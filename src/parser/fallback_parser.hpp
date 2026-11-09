/**
 * @file
 * @brief A parser that tries exact phrase rules first and a second parser for everything else.
 */

#pragma once

#include "commands/command.hpp"

#include <spdlog/spdlog.h>

/**
 * @brief Asks a precise parser first and a broader one only when the first finds no command.
 *
 * Phrase rules resolve the phrasings they list the same way every time, while a language model
 * copes with wording no rule anticipated but may vary with its prompt. Ambiguous text, which the
 * rules decline, also goes to the second parser.
 */
class FallbackParser final : public Commands::Parser {

    /// Tried first; not owned.
    Commands::Parser& first;

    /// Tried when the first finds nothing; not owned.
    Commands::Parser& second;

public:
    /**
     * @brief Try first, then second.
     */
    FallbackParser(Commands::Parser& first, Commands::Parser& second) : first(first), second(second) {}

    /**
     * @brief Configure both parsers.
     */
    void configure(const Commands::json& configuration) override {
        first.configure(configuration);
        second.configure(configuration);
    }

    /**
     * @brief The first parser's command, or else the second's.
     */
    std::optional<Commands::Command> parse(const Commands::string& text, const Commands::Context& context) override {
        if(auto command = first.parse(text, context)) {
            spdlog::debug("Matched by a phrase rule");
            return command;
        }

        return second.parse(text, context);
    }

    /**
     * @brief The second parser's reason, as it is the last one asked.
     */
    Commands::string failureReason() const override { return second.failureReason(); }
};