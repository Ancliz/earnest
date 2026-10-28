/**
 * @file
 * @brief Default-microphone resource management and background capture.
 */

#include "microphone.hpp"

#include <chrono>
#include <stdexcept>
#include <utility>

using Audio::blockSamples;
using Audio::sampleRate;
using Audio::Samples;
using std::lock_guard;
using std::mutex;
using std::runtime_error;
using std::thread;
using std::unique_lock;

namespace {
/**
 * @brief Translate a failing PortAudio status into an application exception.
 *
 * @param[in] error PortAudio return code to inspect.
 * @throws std::runtime_error With PortAudio's error text when error is not paNoError.
 * @note Does not print, close resources, or change capture state on its own.
 */
void checkAudio(PaError error) {
    if(error != paNoError) {
        throw runtime_error(Pa_GetErrorText(error));
    }
}
} // namespace

// Capture continues while transcription and HTTP requests run; the queue holds at most five seconds.
void Microphone::capture() {
    while(!stopping) {
        Samples block(blockSamples);
        const auto error = Pa_ReadStream(stream, block.data(), blockSamples);

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

Microphone::Microphone() {
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
        checkAudio(
            Pa_OpenStream(&stream, &input, nullptr, sampleRate, blockSamples, paNoFlag, nullptr, nullptr));
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

Microphone::~Microphone() {
    stopping = true;

    if(stream) {
        Pa_AbortStream(stream);
    }

    if(worker.joinable()) {
        worker.join();
    }

    if(stream) {
        Pa_CloseStream(stream);
    }

    Pa_Terminate();
}

bool Microphone::read(Samples& block, bool& discontinuity) {
    unique_lock<mutex> lock(audioMutex);

    /*
     * @brief Wake the waiting reader when captured audio or a stored error is available.
     *
     * @return True when the captured queue is nonempty or capture has recorded a failure.
     * @note Evaluates the condition while audioMutex is held; does not consume the queue.
     */
    ready.wait_for(lock, std::chrono::milliseconds(100),
                   [&] { return !pending.empty() || failure != paNoError; });

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