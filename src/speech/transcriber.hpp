/**
 * @file
 * @brief Speech-to-text contract, independent of the recognition engine.
 */

#pragma once

#include "audio/audio.hpp"
#include <string>

namespace Speech {
/**
 * @brief Converts one complete utterance into text.
 */
class Transcriber {
public:
    /**
     * @brief Destroy a transcriber through the interface.
     */
    virtual ~Transcriber() = default;

    /**
     * @brief Recognize the speech in one utterance.
     * @param[in] utterance Mono float samples at Audio::sampleRate.
     * @return Recognized text, possibly empty or padded with spaces.
     * @throws std::exception If recognition fails.
     */
    virtual std::string transcribe(Audio::Samples utterance) = 0;
};
} // namespace Speech