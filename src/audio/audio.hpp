/**
 * @file
 * @brief Shared mono audio sample representation and capture timing constants.
 */

#pragma once

#include <cstdint>
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

/**
 * @brief Float32 PCM at any rate, interleaved if multichannel.
 *
 * sampleRate and channels must both be greater than zero, and samples.size() divisible by channels.
 */
struct Buffer {

    /// Interleaved samples.
    std::vector<float> samples;

    /// Samples per second, per channel.
    std::uint32_t sampleRate;

    /// Interleaved channel count.
    std::uint16_t channels;
};

/**
 * @brief Plays audio.
 */
class Player {
public:
    /**
     * @brief Destroy a player through the interface.
     */
    virtual ~Player() = default;

    /**
     * @brief Play the audio, blocking until it finishes.
     */
    virtual void play(const Buffer& audio) = 0;
};

}