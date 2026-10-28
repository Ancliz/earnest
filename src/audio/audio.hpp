/**
 * @file
 * @brief Shared mono audio sample representation and capture timing constants.
 */

#pragma once

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
}