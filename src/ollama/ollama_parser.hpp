/**
 * @file
 * @brief LLM parser backend.
 */

#pragma once

#include "ollama.hpp"
#include "commands/command.hpp"

using Commands::Command;
using Commands::Context;

/**
 * @brief Asks an Ollama model to choose the one command a text requests.
 *
 * Commands::serviceCatalog() lists the services a command may name, and Context::devices the
 * entities it may target.
 */
class OllamaParser final : public Commands::Parser {

    /// Connection to the model server.
    OllamaClient client;

public:
    /**
     * @brief Apply the phrase file's settings, such as aliases.
     */
    void configure(const json& configuration) override;

    /**
     * @brief Ask the model for the single command the text requests.
     */
    std::optional<Command> parse(const std::string& text, const Context& context) override;
};