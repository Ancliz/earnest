#pragma once

#include <vector>

namespace Audio {
using Samples = std::vector<float>;
constexpr int sampleRate = 16000;
constexpr int blockSamples = sampleRate / 10; // 100 ms
}