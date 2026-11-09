/**
 * @file
 * @brief WAV decoding through dr_wav.
 */

#include "wav.hpp"

#define DR_WAV_IMPLEMENTATION
#include <dr_libs/dr_wav.h>

#include <cstdint>
#include <memory>
#include <stdexcept>

using std::runtime_error;

namespace Audio {
Buffer decodeWav(std::string_view bytes) {
    unsigned int channels = 0;
    unsigned int rate = 0;
    drwav_uint64 frames = 0;
    const auto freeSamples = [](float* samples) { drwav_free(samples, nullptr); };
    std::unique_ptr<float, decltype(freeSamples)> samples{drwav_open_memory_and_read_pcm_frames_f32(bytes.data(), bytes.size(), &channels, &rate, &frames, nullptr), freeSamples};

    if(!samples) {
        throw runtime_error("Failed to decode WAV");
    } else if(channels == 0 || channels > 65535) {
        throw runtime_error("Invalid channel count");
    }

    Buffer buffer;
    buffer.sampleRate = rate;
    buffer.channels = static_cast<std::uint16_t>(channels);
    buffer.samples.assign(samples.get(), samples.get() + frames * channels);
    return buffer;
}
} // namespace Audio