#include <memory>
#include <stdexcept>
#include <cstdint>
#include <string_view>
#include "audio.hpp"

#define DR_WAV_IMPLEMENTATION
#include <dr_libs/dr_wav.h>


#include <memory>
#include <stdexcept>

namespace Audio {
    
Audio::Buffer decodeWav(std::string_view bytes) {
    unsigned int channels = 0;
    unsigned int sampleRate = 0;
    drwav_uint64 frames = 0;

    auto freeSamples = [](float* p) { drwav_free(p, nullptr); };

    std::unique_ptr<float, decltype(freeSamples)> samples{
        drwav_open_memory_and_read_pcm_frames_f32(
            bytes.data(), bytes.size(),
            &channels, &sampleRate, &frames,
            nullptr
        ),
        freeSamples
    };

    if(!samples)
        throw std::runtime_error("Failed to decode WAV");

    if(channels == 0 || channels > 65535)
        throw std::runtime_error("Invalid channel count");

    Audio::Buffer buffer;
    buffer.sampleRate = sampleRate;
    buffer.channels = static_cast<std::uint16_t>(channels);
    buffer.samples.assign(
        samples.get(),
        samples.get() + frames * channels
    );

    return buffer;
}
}