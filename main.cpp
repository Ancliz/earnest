#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <csignal>
#include <cstdlib>
#include <cstddef>
#include <deque>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <portaudio.h>
#include <whisper.h>
#include <httplib.h>
#include "parser.hpp"
#include "device_aliases.hpp"

using std::atomic;
using std::condition_variable;
using std::deque;
using std::exception;
using std::lock_guard;
using std::mutex;
using std::optional;
using std::runtime_error;
using std::sig_atomic_t;
using std::size_t;
using std::thread;
using std::unique_lock;
using std::string;
using std::unique_ptr;
using std::vector;
using Parser::json;

namespace {

volatile sig_atomic_t running = 1;
constexpr int sampleRate = WHISPER_SAMPLE_RATE;
constexpr int frames = sampleRate / 10; // 100 ms, mono float32
const string HOME_ASSISTANT_URL = "http://wyvern";
const int HOME_ASSISTANT_PORT = 8123;

void handleSignal(int) {
    running = 0;
}

void checkAudio(PaError error) {
    if(error != paNoError) {
        throw runtime_error(Pa_GetErrorText(error));
    }
}

struct Settings {
    string model = "models/ggml-base.en.bin";
    int threads = static_cast<int>(std::max(1u, std::min(4u, thread::hardware_concurrency())));
    float threshold = 0.01f;
    int silenceMs = 700;
    bool gpu = true;
};

Settings arguments(int argc, char* argv[]) {
    Settings settings;
    bool modelSet = false;

    for(int i = 1; i < argc; ++i) {
        const string arg = argv[i];

        if(arg == "--cpu") {
            settings.gpu = false;
            continue;
        }

        if(arg.empty() || arg[0] != '-') {
            if(modelSet) {
                throw runtime_error("Only one model path is accepted");
            }

            settings.model = arg;
            modelSet = true;
            continue;
        }

        if(arg != "--threads" && arg != "--threshold" && arg != "--silence-ms") {
            throw runtime_error("Unknown option: " + arg);
        }

        if(++i == argc) {
            throw runtime_error("Missing value for " + arg);
        }

        const string value = argv[i];
        size_t consumed = 0;

        if(arg == "--threads") {
            settings.threads = std::stoi(value, &consumed);
        } else if(arg == "--threshold") {
            settings.threshold = std::stof(value, &consumed);
        } else {
            settings.silenceMs = std::stoi(value, &consumed);
        }

        if(consumed != value.size()) {
            throw runtime_error("Invalid value for " + arg);
        }
    }

    if(settings.threads < 1 || settings.threads > 256 || !std::isfinite(settings.threshold) ||
        settings.threshold <= 0 || settings.threshold >= 1 ||
        settings.silenceMs < 200 || settings.silenceMs > 3000) {
        throw runtime_error("Expected threads 1..256, threshold 0..1, silence-ms 200..3000");
    }

    return settings;
}

// Capture continues while Whisper transcribes. A bounded queue prevents stale commands
// accumulating indefinitely when the model cannot keep up with real time.
class Microphone {
    PaStream* stream = nullptr;
    thread worker;
    atomic<bool> stopping{false};
    mutex audioMutex;
    condition_variable ready;
    deque<vector<float>> pending;
    PaError failure = paNoError;
    bool lostAudio = false;

    void capture() {
        while(!stopping) {
            vector<float> block(frames);
            const auto error = Pa_ReadStream(stream, block.data(), frames);

            {
                lock_guard<mutex> lock(audioMutex);

                if(error != paNoError && error != paInputOverflowed) {
                    failure = error;
                    ready.notify_one();
                    return;
                }

                if(error == paInputOverflowed || pending.size() >= 50) {
                    pending.clear();
                    lostAudio = true;
                }

                pending.push_back(std::move(block));
            }

            ready.notify_one();
        }
    }

public:
    Microphone() {
        checkAudio(Pa_Initialize());

        try {
            const auto device = Pa_GetDefaultInputDevice();

            if(device == paNoDevice) {
                throw runtime_error("No default microphone found");
            }

            const auto* info = Pa_GetDeviceInfo(device);

            if(!info) {
                throw runtime_error("Cannot read microphone information");
            }

            PaStreamParameters input{};
            input.device = device;
            input.channelCount = 1;
            input.sampleFormat = paFloat32;
            input.suggestedLatency = info->defaultHighInputLatency;
            checkAudio(Pa_IsFormatSupported(&input, nullptr, sampleRate));
            checkAudio(Pa_OpenStream(&stream, &input, nullptr, sampleRate, frames,paNoFlag, nullptr, nullptr));
            checkAudio(Pa_StartStream(stream));
            worker = thread(&Microphone::capture, this);
        } catch(...) {
            if(stream) {
                Pa_CloseStream(stream);
            }

            Pa_Terminate();
            throw;
        }
    }

    ~Microphone() {
        stopping = true;

        if(worker.joinable()) {
            worker.join();
        }

        if(stream) {
            Pa_StopStream(stream);
            Pa_CloseStream(stream);
        }

        Pa_Terminate();
    }

    Microphone(const Microphone&) = delete;
    Microphone& operator=(const Microphone&) = delete;

    bool read(vector<float>& block, bool& discontinuity) {
        unique_lock<mutex> lock(audioMutex);

        ready.wait_for(lock, std::chrono::milliseconds(100), [&] {
            return !pending.empty() || failure != paNoError;
        });

        checkAudio(failure);

        if(pending.empty()) {
            return false;
        }

        discontinuity = lostAudio;
        lostAudio = false;
        block = std::move(pending.front());
        pending.pop_front();
        return true;
    }
};

class Transcriber {
    unique_ptr<whisper_context, decltype(&whisper_free)> context{nullptr, whisper_free};
    whisper_full_params parameters;

public:
    explicit Transcriber(const Settings& settings)
        : parameters(whisper_full_default_params(WHISPER_SAMPLING_GREEDY)) {
        auto config = whisper_context_default_params();
        config.use_gpu = settings.gpu;
        context.reset(whisper_init_from_file_with_params(settings.model.c_str(), config));

        if(!context) {
            throw runtime_error("Failed to load Whisper model: " + settings.model);
        }

        parameters.n_threads = settings.threads;
        parameters.language = "en";
        parameters.translate = false;
        parameters.no_context = true;
        parameters.no_timestamps = true;
        parameters.print_realtime = false;
        parameters.print_progress = false;
        parameters.print_timestamps = false;
        parameters.print_special = false;
        parameters.suppress_blank = true;
        parameters.suppress_nst = true;
        parameters.temperature_inc = 0; // Avoid costly fallback retries for short commands
    }

    string transcribe(vector<float> audio) {
        // Short commands still need a minimum audio window for Whisper
        if(audio.size() < sampleRate) {
            audio.resize(sampleRate, 0);
        }

        if(whisper_full(context.get(), parameters, audio.data(), static_cast<int>(audio.size())) != 0) {
            throw runtime_error("Whisper transcription failed");
        }

        string text;

        for(int i = 0; i < whisper_full_n_segments(context.get()); ++i) {
            text += whisper_full_get_segment_text(context.get(), i);
        }

        return text;
    }
};

class HomeAssistant {
    httplib::Client client;

public:
    explicit HomeAssistant(
        const string& url = HOME_ASSISTANT_URL + ":" + std::to_string(HOME_ASSISTANT_PORT),
        const char* token = std::getenv("HOME_ASSISTANT_TOKEN")) : client(url) {
        if(!token || !*token) {
            throw runtime_error("Set HOME_ASSISTANT_TOKEN before starting voice control");
        }

        client.set_default_headers({{"Authorization", string("Bearer ") + token}});
        client.set_connection_timeout(5);
        client.set_read_timeout(10);
        client.set_write_timeout(10);
    }

    json getStates() {
        const auto response = client.Get("/api/states");

        if(!response) {
            throw runtime_error("Cannot query Home Assistant states: " + httplib::to_string(response.error()));
        }

        if(response->status != 200) {
            throw runtime_error("Home Assistant states returned HTTP " + std::to_string(response->status));
        }

        auto states = json::parse(response->body, nullptr, false);

        if(!states.is_array()) {
            throw runtime_error("Home Assistant returned invalid states JSON");
        }

        return states;
    }

    void post(const json& request) {
        const string endpoint = request["endpoint"].get<string>();
        const auto& body = request["body"];

        const auto response = client.Post("/api" + endpoint, body.dump(), "application/json");

        if(!response) {
            std::cerr << "Home Assistant request failed: " << httplib::to_string(response.error()) << '\n';
            return;
        }

        if(response->status < 200 || response->status >= 300) {
            std::cerr << response->status << ": " << response->body << '\n';
            return;
        }

        if(endpoint == "/conversation/process") {
            const auto result = json::parse(response->body, nullptr, false);

            if(result.is_discarded() || !result.is_object() || !result.contains("response") ||
                !result["response"].is_object()) {
                std::cerr << "Home Assistant returned an invalid conversation response\n";
                return;
            }

            if(result["response"].value("response_type", string()) == "error") {
                std::cerr << "Home Assistant could not handle the command: " << result["response"] << '\n';
                return;
            }
        }

        std::cout << "Command sent\n";
    }
};

void printCommand(const string& text, const json& request) {
    std::cout << "Final: " << text << std::endl;

    if(request.is_null()) {
        std::cout << "No command recognized\n";
    } else {
        std::cout << "Command recognized: " << request << std::endl;
    }
}

void processUtterance(vector<float> audio, Transcriber& transcriber,
                      HomeAssistant& assistant, const Parser::Options& options) {
    const string text = transcriber.transcribe(std::move(audio));

    if(text.find_first_not_of(" \t\r\n") == string::npos) {
        return;
    }

    const json request = Parser::parse(text, options);
    printCommand(text, request);

    if(!request.is_null()) {
        assistant.post(request);
    }
}

class UtteranceBuffer {
    vector<float> audio;
    deque<vector<float>> preroll;
    int voicedFrames = 0;
    int silentFrames = 0;
    int silenceFrames;
    float threshold;
    bool discardUntilSilence = false;

    void clear() {
        audio.clear();
        preroll.clear();
        voicedFrames = silentFrames = 0;
    }

public:
    explicit UtteranceBuffer(const Settings& settings)
        : silenceFrames((settings.silenceMs + 99) / 100), threshold(settings.threshold) {}

    optional<vector<float>> finish() {
        optional<vector<float>> completed;

        if(!discardUntilSilence && voicedFrames >= 2) {
            completed = std::move(audio);
        }

        clear();
        return completed;
    }

    optional<vector<float>> append(const vector<float>& block, bool discontinuity) {
        if(discontinuity) {
            std::cerr << "Microphone overflow: discarding incomplete command\n";
            clear();
            discardUntilSilence = true;
        }

        double energy = 0;

        for(float sample : block) {
            energy += sample * sample;
        }

        const bool speech = !block.empty() && std::sqrt(energy / block.size()) >= threshold;

        if(discardUntilSilence) {
            silentFrames = speech ? 0 : silentFrames + 1;

            if(silentFrames >= silenceFrames) {
                discardUntilSilence = false;
                silentFrames = 0;
            }

            return std::nullopt;
        }

        if(audio.empty() && !speech) {
            preroll.push_back(block);

            if(preroll.size() > 2) {
                preroll.pop_front();
            }

            return std::nullopt;
        }

        if(audio.empty()) {
            for(const auto& previous : preroll) {
                audio.insert(audio.end(), previous.begin(), previous.end());
            }

            preroll.clear();
        }

        audio.insert(audio.end(), block.begin(), block.end());
        voicedFrames += speech;
        silentFrames = speech ? 0 : silentFrames + 1;

        if(silentFrames >= silenceFrames) {
            return finish();
        }

        if(audio.size() >= 15 * sampleRate) {
            std::cerr << "Utterance exceeded 15 seconds; waiting for silence\n";
            clear();
            discardUntilSilence = true;
        }

        return std::nullopt;
    }
};

} // namespace

int main(int argc, char* argv[]) {
    if(argc == 2 && (string(argv[1]) == "--help" || string(argv[1]) == "-h")) {
        std::cout << "Usage: " << argv[0] << " [model.bin] [--threads N] [--threshold RMS] "
                     "[--silence-ms N] [--cpu]\n";
        return 0;
    }

    try {
        const auto settings = arguments(argc, argv);
        HomeAssistant assistant;
        Parser::Options options;
        populateDeviceAliases(assistant.getStates(), options);
        Transcriber transcriber(settings);
        std::signal(SIGINT, handleSignal);
        std::signal(SIGTERM, handleSignal);
        Microphone microphone;
        UtteranceBuffer utterances(settings);
        vector<float> block;
        std::cout << "Listening. Press Ctrl+C to stop.\n";

        while(running) {
            bool discontinuity = false;

            if(!microphone.read(block, discontinuity)) {
                continue;
            }

            if(auto audio = utterances.append(block, discontinuity)) {
                processUtterance(std::move(*audio), transcriber, assistant, options);
            }
        }

        if(auto audio = utterances.finish()) {
            processUtterance(std::move(*audio), transcriber, assistant, options);
        }
    } catch(const exception& error) {
        std::cerr << "Error: " << error.what() << '\n';
        return 1;
    }

    return 0;
}
