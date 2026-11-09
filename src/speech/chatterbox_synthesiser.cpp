/**
 * @file
 * @brief Chatterbox server requests.
 */

#include "chatterbox_synthesiser.hpp"
#include "audio/wav.hpp"

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <stdexcept>

using std::runtime_error;
using std::string;

namespace Speech {
Buffer ChatterboxSynthesiser::generate(string_view text) {
    spdlog::debug("Requesting speech: {} bytes of text", text.size());
    const nlohmann::json body = {{"input", string(text)}, {"voice", voice}};
    const auto response = client.Post("/v1/audio/speech", body.dump(), "application/json");

    if(!response) {
        throw runtime_error("Cannot reach the speech server: " + httplib::to_string(response.error()));
    } else if(response->status != 200) {
        throw runtime_error("Failed to generate speech: HTTP " + std::to_string(response->status) + ": " + response->body);
    }

    spdlog::debug("Speech server returned {} bytes", response->body.size());
    return Audio::decodeWav(response->body);
}
} // namespace Speech