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
 * @brief Throw PortAudio's error text for any status other than paNoError.
 */
void throwIfError(PaError error) {
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
    throwIfError(Pa_Initialize());

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
        throwIfError(Pa_IsFormatSupported(&input, nullptr, sampleRate));
        throwIfError(
            Pa_OpenStream(&stream, &input, nullptr, sampleRate, blockSamples, paNoFlag, nullptr, nullptr));
        throwIfError(Pa_StartStream(stream));
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

    // Abort before joining so a blocked read returns.
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
    ready.wait_for(lock, std::chrono::milliseconds(100),
                   [&] { return !pending.empty() || failure != paNoError; });
    throwIfError(failure);

    if(pending.empty()) {
        return false;
    }

    discontinuity = lostAudio;
    lostAudio = false;
    block = std::move(pending.front());
    pending.pop_front();
    return true;
}

void Microphone::discardPending() {
    lock_guard<mutex> lock(audioMutex);
    pending.clear();
    lostAudio = false;
}