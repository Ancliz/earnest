/**
 * @file
 * @brief Shared mono audio sample representation and capture timing constants.
 */

#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

namespace Audio {
/**
 * @brief Mono float32 samples at sampleRate.
 */
using Samples = std::vector<float>;

/// Capture and recognition rate, in samples per second.
constexpr int sampleRate = 16000;

/// Samples in one 100 ms capture block.
constexpr int blockSamples = sampleRate / 10;

/// Float32 PCM, interleaved if multichannel.
/// sampleRate and channels must both be greater than zero.
/// samples.size() must be divisible by channels.
struct Buffer {
    std::vector<float> samples;
    std::uint32_t sampleRate;
    std::uint16_t channels;
};

class Player {
public:
    virtual ~Player() = default;

    virtual void play(const Buffer& audio) = 0;
};

Buffer decodeWav(std::string_view bytes);

}