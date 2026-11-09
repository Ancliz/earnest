/**
 * @file
 * @brief Speech-to-text contract, independent of the recognition engine.
 */

#pragma once

#include "audio/audio.hpp"
#include <string>
#include <vector>

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

    /**
     * @brief Bias recognition towards words that commands use; engines without biasing ignore it.
     * @param[in] words Words to favour, such as device names; empty clears the bias.
     */
    virtual void setVocabulary(const std::vector<std::string>&) {}
};
} // namespace Speech