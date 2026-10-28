/**
 * @file
 * @brief HTTP client contract for Ollama chat and tool-calling inference.
 */

#pragma once

#include <httplib.h>
#include <nlohmann/json.hpp>
#include <string>

using std::string;
using nlohmann::json;

/**
 * @brief Own the connection and model selection used for Ollama inference.
 *
 * This client transports chat messages and tool definitions. A command-parser
 * adapter interprets the returned model response; this class does not resolve
 * Home Assistant entities, validate control policy, or execute tools.
 *
 * @note Declarations only; implement these operations in ollama.cpp.
 */
class Ollama {
    httplib::Client client;
    string model;

public:
    /**
     * @brief Configure the inference endpoint and model without making a request.
     *
     * @param[in] url Ollama server origin, including scheme and optional port.
     *                Use the server's LAN address when it runs on another computer.
     * @param[in] model Installed model name/tag to include in inference requests.
     * @note The implementation should configure client timeouts here. It should
     *       not download a model, start inference, or contact Home Assistant.
     */
    explicit Ollama(const string& url = "http://192.168.4.238:11434",
                    const string& model = "qwen3.5:4b");

    /**
     * @brief Submit messages and optional tools to Ollama's chat endpoint.
     *
     * @param[in] messages Chat-message array containing roles and message content.
     * @param[in] tools Function-tool definitions available to the model; an empty
     *                  array requests a conversation without tools.
     * @return Full decoded response envelope, including message content or tool calls.
     * @throws std::runtime_error The implementation should report transport failures,
     *                           non-success HTTP statuses, and invalid responses.
     * @note Implement a non-streaming request so the caller receives one JSON result.
     *       This performs inference only; returned tool calls must not be executed here.
     */
    json chat(const json& messages, const json& tools = json::array());
};
