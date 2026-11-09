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

public:
    /**
     * @brief Configure the Ollama server and model without making a request.
     */
    OllamaParser(const string& url, const string& model) : client(url, model) {}

    /**
     * @brief Apply the phrase file's settings, such as the registry.
     */
    void configure(const json& configuration) override;

    /**
     * @brief Ask the model for the single command the text requests.
     */
    Commands::ParseResult parse(const string& text, const Context& context) override;
};