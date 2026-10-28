/**
 * @file
 * @brief Regex parser backend configured from the phrase file.
 */

#pragma once

#include "phrase_rules.hpp"

/**
 * @brief Parses text with the phrase file's ordered rules and resolves device names itself.
 */
class RegexParser final : public Commands::Parser {

    /// Compiled rules in configuration order.
    std::vector<Grammar::Rule> rules;
    
    /// Configured spoken names by entity ID.
    Commands::json aliases = Commands::json::object();

public:
    /**
     * @brief Construct a parser with no rules; call configure() before parsing.
     */
    RegexParser() = default;

    /**
     * @brief Construct and configure a parser.
     * @throws std::exception If the configuration is invalid.
     */
    explicit RegexParser(const Commands::json& configuration);

    /**
     * @brief Compile the configuration's rules and aliases, replacing the active ones only on success.
     */
    void configure(const Commands::json& configuration) override;

    /**
     * @brief Match normalized text against every rule.
     *
     * Rejects negative numbers, negations and conjunctions that could hide a second command.
     * The first matching pattern of each domain proposes a command; differing proposals from
     * several domains make the text ambiguous.
     */
    std::optional<Commands::Command> parse(const std::string& text,
                                           const Commands::Context& context) override;
};