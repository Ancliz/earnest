/**
 * @file
 * @brief LLM parser backend.
 */

#pragma once

#include "ollama.hpp"
#include "commands/command.hpp"
#include "home/registry.hpp"

using Commands::Command;
using Commands::Context;

/**
 * @brief Asks an Ollama model to choose the one command a text requests.
 *
 * Context::services lists the services a command may name, and Context::devices the
 * entities it may target.
 */
class OllamaParser final : public Commands::Parser {

    /// Connection to the model server.
    OllamaClient client;

    /// Configured rooms, physical devices and spoken names.
    Home::Registry registry;

    /// Whether to load the unchanging part of the prompt before each request; see OllamaClient::sendPrerequest().
    bool prerequest;

    /// How freely answers are worded; commands are always generated at 0.
    double answerTemperature;

    /**
     * @brief Generate an answer freely, passing on each sentence as it is finished.
     * @param[in] messages The same messages that were just found to want an answer.
     * @throws std::runtime_error If it is cut off before its first sentence ends, or for the client's failures.
     */
    Commands::ParseResult speakAnswer(const json& messages, const Context& context);

public:
    /**
     * @brief Configure the Ollama server and model without making a request.
     * @param[in] prerequest Whether to speed up requests in a way specific to qwen3.5:4b.
     * @param[in] keepLoaded Whether to ask the server never to unload the model.
     * @param[in] answerTemperature How freely answers are worded.
     */
    OllamaParser(const string& url, const string& model, bool prerequest = true, bool keepLoaded = false, double answerTemperature = 1.0)
        : client(url, model, keepLoaded), prerequest(prerequest), answerTemperature(answerTemperature) {}

    /**
     * @brief Apply the phrase file's settings, such as the registry.
     */
    void configure(const json& configuration) override;

    /**
     * @brief Ask the model for the single command the text requests.
     */
    Commands::ParseResult parse(const string& text, const Context& context) override;
};