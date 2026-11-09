/**
 * @file
 * @brief Phrase DSL compilation and validation of configured command rules.
 */

#pragma once

#include "commands/command.hpp"
#include <map>
#include <regex>
#include <vector>

namespace Grammar {
/// JSON value type shared with the command contracts.
using Commands::json;
using std::string;

/**
 * @brief One compiled phrase or regex.
 */
struct Pattern {

    /// Full-match expression.
    std::regex expression;

    /// Capture-group index for each named slot, such as device or volume.
    std::map<string, int> slots;
};

/**
 * @brief One configured rule: the patterns that match it and the command it produces.
 */
struct Rule {

    /// Unique rule identifier, used in error messages.
    string id;

    /// Command domain.
    string domain;

    /// Command service.
    string service;

    /// Service data template; whole-string "{slot}" values are filled from captures.
    json body;

    /// Spoken words mapped to service values, such as "cooling" to "cool".
    json values;

    /// What to say on success; "{slot}" takes a captured word. Empty for no particular wording.
    string reply;

    /// Whether a captured number is a volume change amount rather than a body value.
    bool relative = false;
    
    /// Patterns tried in order.
    std::vector<Pattern> patterns;
};

/**
 * @brief Slot named by a whole-string "{slot}" template value.
 * @return Slot name, or empty for literal values.
 */
string placeholderName(const json& value);

/**
 * @brief Validate a version-1 phrase configuration and compile its rules in order.
 * @throws std::runtime_error For invalid rules, slots or services.
 * @throws std::regex_error If a phrase or regex cannot compile.
 * @throws nlohmann::json::exception For missing fields or wrong types.
 */
std::vector<Rule> compileRules(const json& configuration);
} // namespace Grammar