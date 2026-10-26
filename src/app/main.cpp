#include "settings.hpp"
#include "audio/microphone.hpp"
#include "audio/utterance_buffer.hpp"
#include "home_assistant/control_policy.hpp"
#include "home_assistant/home_assistant.hpp"

#include <chrono>
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <utility>
#include <memory>
#include <stdexcept>
#include <whisper.h>

using Audio::Samples;
using Parser::json;
using std::string;

namespace {
using Audio::sampleRate;
using std::runtime_error;

class Transcriber {
    std::unique_ptr<whisper_context, decltype(&whisper_free)> context{nullptr, whisper_free};
    whisper_full_params parameters;

public:
    explicit Transcriber(const Settings& settings);
    string transcribe(Audio::Samples audio);
};

static_assert(sampleRate == WHISPER_SAMPLE_RATE);

Transcriber::Transcriber(const Settings& settings) : parameters(whisper_full_default_params(WHISPER_SAMPLING_GREEDY)) {
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

string Transcriber::transcribe(Samples audio) {
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

void debugPrintTranscript(const string& text, const json& request) {
    std::cout << "Final: " << text << std::endl;

    if(request.is_null()) {
        std::cout << "No command recognized\n";
    } else {
        std::cout << "Command recognized: " << request << std::endl;
    }
}

bool processTranscript(const string& text, HomeAssistant& assistant, Parser::Engine& phrases,
                       Parser::Options& options, bool send = true) {
    if(text.find_first_not_of(" \t\r\n") == string::npos) {
        return false;
    }

    json states;

    try {
        states = assistant.getStates();
        populateDeviceAliases(states, options);
    } catch(const std::exception& error) {
        std::cerr << "Cannot refresh device discovery: " << error.what() << '\n';
        return false;
    }

    phrases.reloadIfChanged();
    const json request = phrases.parse(text, options);
    debugPrintTranscript(text, request);

    if(request.is_null()) {
        return false;
    }

    if(send) {
        return assistant.post(request, phrases.configuration());
    }

    const string reason = validateCommand(request, states, phrases.configuration());

    if(!reason.empty()) {
        std::cout << "Would reject: " << reason << '\n';
        return false;
    }

    return true;
}

void processUtterance(Samples audio, Transcriber& transcriber, HomeAssistant& assistant,
                      Parser::Engine& phrases, Parser::Options& options) {
    processTranscript(transcriber.transcribe(std::move(audio)), assistant, phrases, options);
}

volatile std::sig_atomic_t running = 1;

void handleSignal(int) {
    running = 0;
}
} // namespace

int main(int argc, char* argv[]) {
    if(argc == 2 && (string(argv[1]) == "--help" || string(argv[1]) == "-h")) {
        printUsage(argv[0]);
        return 0;
    }

    try {
        const auto settings = parseArguments(argc, argv);
        Parser::Engine phrases(settings.phrases);

        if(settings.checkPhrases) {
            std::cout << "Phrase configuration is valid\n";
            return 0;
        }

        loadEnvironment();
        const char* configuredUrl = std::getenv("HOME_ASSISTANT_URL");
        HomeAssistant assistant(configuredUrl ? configuredUrl : "http://wyvern:8123",
                                std::getenv("HOME_ASSISTANT_TOKEN"));
        Parser::Options options;

        if(settings.listDevices) {
            for(const auto& device : findAvailableDevices(assistant.getStates())) {
                std::cout << device.entityId << " | " << device.friendlyName << " | " << device.state << '\n';
            }

            return 0;
        }

        if(settings.parseOnly) {
            return processTranscript(settings.parseText, assistant, phrases, options, false) ? 0 : 1;
        }

        populateDeviceAliases(assistant.getStates(), options);
        Transcriber transcriber(settings);
        std::signal(SIGINT, handleSignal);
        std::signal(SIGTERM, handleSignal);
        Microphone microphone;
        UtteranceBuffer utterances(settings);
        Samples block;
        std::cout << "Listening. Press Ctrl+C to stop.\n";
        auto lastReload = std::chrono::steady_clock::now();
        string lastReloadError;

        while(running) {
            const auto now = std::chrono::steady_clock::now();

            if(now - lastReload >= std::chrono::seconds(1)) {
                if(phrases.reloadIfChanged()) {
                    std::cout << "Reloaded phrase configuration\n";
                }

                if(phrases.reloadError() != lastReloadError && !phrases.reloadError().empty()) {
                    std::cerr << "Keeping previous phrases: " << phrases.reloadError() << '\n';
                }

                lastReloadError = phrases.reloadError();
                lastReload = now;
            }

            bool discontinuity = false;

            if(!microphone.read(block, discontinuity)) {
                continue;
            }

            if(auto audio = utterances.append(block, discontinuity)) {
                processUtterance(std::move(*audio), transcriber, assistant, phrases, options);
            }
        }

        if(auto audio = utterances.finish()) {
            processUtterance(std::move(*audio), transcriber, assistant, phrases, options);
        }
    } catch(const std::exception& error) {
        std::cerr << "Error: " << error.what() << '\n';
        return 1;
    }

    return 0;
}