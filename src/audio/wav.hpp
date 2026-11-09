/**
 * @file
 * @brief WAV decoding.
 */

#pragma once

#include "audio/audio.hpp"

#include <string_view>

namespace Audio {
/**
 * @brief Decode a complete WAV file held in memory.
 * @throws std::runtime_error If the bytes are not a valid WAV file.
 */
Buffer decodeWav(std::string_view bytes);
} // namespace Audio