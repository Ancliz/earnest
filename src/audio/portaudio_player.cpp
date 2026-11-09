/**
 * @file
 * @brief PortAudio output stream handling.
 */

#include "portaudio_player.hpp"

#include <algorithm>
#include <memory>
#include <portaudio.h>
#include <stdexcept>

using std::runtime_error;
using std::size_t;

namespace {
/// Frames handed to PortAudio at a time.
constexpr size_t framesPerWrite = 1024;

/**
 * @brief Turn a PortAudio error code into an exception.
 */
void check(PaError error) {
    if(error != paNoError) {
        throw runtime_error(Pa_GetErrorText(error));
    }
}
} // namespace

namespace Audio {
PortAudioPlayer::PortAudioPlayer() {
    check(Pa_Initialize());
}

PortAudioPlayer::~PortAudioPlayer() {
    Pa_Terminate();
}

void PortAudioPlayer::play(const Buffer& buffer) {
    if(buffer.samples.empty()) {
        return;
    }

    if(buffer.channels == 0 || buffer.sampleRate == 0 || buffer.samples.size() % buffer.channels != 0) {
        throw runtime_error("Invalid audio buffer");
    }

    PaStream* raw = nullptr;
    check(Pa_OpenDefaultStream(&raw, 0, buffer.channels, paFloat32, buffer.sampleRate, framesPerWrite, nullptr, nullptr));

    // Closes the stream, including when an exception occurs.
    std::unique_ptr<PaStream, decltype(&Pa_CloseStream)> stream(raw, &Pa_CloseStream);
    check(Pa_StartStream(stream.get()));
    const auto totalFrames = buffer.samples.size() / buffer.channels;

    for(size_t offset = 0; offset < totalFrames;) {
        const auto frames = static_cast<unsigned long>(std::min(framesPerWrite, totalFrames - offset));
        const auto error = Pa_WriteStream(stream.get(), buffer.samples.data() + offset * buffer.channels, frames);

        // An underflow reports a playback gap; the write still completes.
        if(error != paOutputUnderflowed) {
            check(error);
        }

        offset += frames;
    }

    // Wait for the final buffered samples to finish playing.
    check(Pa_StopStream(stream.get()));
}
} // namespace Audio