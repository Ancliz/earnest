/**
 * @file
 * @brief HTTP client for Ollama chat and tool-calling inference.
 */

#pragma once

#include <httplib.h>
#include <nlohmann/json.hpp>
#include <string>

using nlohmann::json;
using nlohmann::ordered_json;
using std::string;

/**
 * @brief Sends chat requests to one Ollama server and model.
 *
 * Transport only: interpreting replies and executing tool calls belong to the caller.
 */
class OllamaClient {

    /// Connection to the Ollama server.
    httplib::Client client;

    /// Model name and tag sent with each request.
    string model;

public:
    /**
     * @brief Configure the endpoint and model without making a request.
     * @param[in] url Server origin, including scheme and port.
     * @param[in] model Installed model name and tag.
     */
    explicit OllamaClient(const string& url = "http://192.168.4.238:11434", const string& model = "qwen3.5:4b");

    /**
     * @brief Submit messages and optional tools to the chat endpoint.
     * @param[in] messages Chat messages with roles and content.
     * @param[in] tools Function-tool definitions; empty for a plain conversation.
     * @param[in] format JSON schema the reply content must match, in the order fields should be
     *                   generated; empty for free text.
     * @return Decoded response, including message content or tool calls.
     * @throws std::runtime_error For transport failures, non-200 statuses, or invalid JSON.
     */
    json chat(const json& messages, const json& tools = json::array(),
              const ordered_json& format = ordered_json::object());
};