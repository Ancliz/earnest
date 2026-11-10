/**
 * @file
 * @brief HTTP client for Ollama chat and tool-calling inference.
 */

#pragma once

#include <functional>
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

    /// Whether to ask the server never to unload the model; otherwise the server's setting applies.
    bool keepLoaded;

public:
    /**
     * @brief Told each piece of generated text; returns false to stop generating.
     */
    using TextReceiver = std::function<bool(const string&)>;

    /**
     * @brief Configure the endpoint and model without making a request.
     * @param[in] url Server origin, including scheme and port.
     * @param[in] model Installed model name and tag.
     * @param[in] keepLoaded Whether to ask the server never to unload the model.
     */
    OllamaClient(const string& url, const string& model, bool keepLoaded = false);

    /**
     * @brief Generate a reply to messages, passing it on piece by piece as it is written.
     * @param[in] messages Chat messages with roles and content.
     * @param[in] format JSON schema the reply must match, in the order fields should be generated.
     * @param[in] temperature How freely to choose words; 0 always gives the likeliest reply.
     * @param[in] receiver Told each piece of text; returning false stops generating.
     * @return The final report with all the text as message.content; only the text if it was stopped.
     * @throws std::runtime_error For transport failures, non-200 statuses, or invalid JSON.
     */
    json chat(const json& messages, const ordered_json& format, double temperature, const TextReceiver& receiver);

    /**
     * @brief Load the unchanging start of a prompt, so that the chat request after it reads only what follows.
     *
     * Written for qwen3.5:4b, and tested on nothing else. That model re-read the whole prompt
     * whenever the end of it differed, and doesn't after this; why isn't established. The text is
     * wrapped by hand in that model's format, so with another model it wouldn't match the real prompt.
     *
     * @param[in] systemStart The start of the system message, the same in every request.
     * @throws std::runtime_error If the server doesn't answer, as the chat request wouldn't work either.
     */
    void sendPrerequest(const string& systemStart);
};