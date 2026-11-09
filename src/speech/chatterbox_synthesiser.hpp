/**
 * @file
 * @brief Speech generation through a Chatterbox server.
 */

#pragma once

#include "speech/synthesiser.hpp"

#include <httplib.h>
#include <string>

namespace Speech {
/**
 * @brief Asks a Chatterbox server, over its OpenAI-style speech endpoint, to voice text.
 */
class ChatterboxSynthesiser final : public Synthesiser {

    /// Connection to the speech server.
    httplib::Client client;

    /// Voice name sent with every request.
    std::string voice;

public:
    /**
     * @brief Configure the server origin and voice without making a request.
     */
    ChatterboxSynthesiser(const std::string& url, const std::string& voice) : client(url), voice(voice) {}

    /**
     * @brief Generate speech for the text.
     * @throws std::runtime_error If the server cannot be reached, refuses, or returns an invalid WAV.
     */
    Buffer generate(string_view text) override;
};
} // namespace Speech