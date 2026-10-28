/**
 * @file
 * @brief Ollama chat requests.
 */

#include "ollama.hpp"

using nlohmann::json;
using std::runtime_error;
using std::string;

OllamaClient::OllamaClient(const string& url, const string& model) : client(url.c_str()), model(model) {
    client.set_connection_timeout(5);
    client.set_read_timeout(30);
    client.set_write_timeout(30);
}

json OllamaClient::chat(const json& messages, const json& tools) {
    // TODO
    json request = {{"model", model}, {"messages", messages}, {"tools", tools}};
    const auto response = client.Post("/api/chat", request.dump(), "application/json");

    if(!response) {
        throw runtime_error("Cannot query Ollama: " + httplib::to_string(response.error()));
    } else if(response->status != 200) {
        throw runtime_error("Ollama returned HTTP " + std::to_string(response->status));
    }

    auto result = json::parse(response->body, nullptr, false);

    if(!result.is_object()) {
        throw runtime_error("Ollama returned invalid JSON");
    }

    return result;
}