/**
 * @file
 * @brief Internal phrase-compilation, text-normalization, and device-resolution contracts.
 */

#pragma once
#include "parser.hpp"
#include <map>
#include <regex>
#include <vector>

namespace Parser::Detail {
using std::regex;

struct Pattern {
    regex expression;
    std::map<string, int> slots;
};

struct Rule {
    string id;
    string domain;
    string service;
    json body;
    json values;
    bool relative = false;
    std::vector<Pattern> patterns;
};

/**
 * @brief Check that a compiled phrase supplies the placeholders used by its rule body.
 *
 * Only whole-field string values enclosed in braces are treated as placeholders.
 * The special "value" placeholder is satisfied by any numeric volume, percentage,
 * or temperature capture. Literal body fields require no capture.
 *
 * @param[in] pattern Compiled expression and capture-slot indexes to inspect.
 * @param[in] rule Rule whose service-body placeholders must be populated.
 * @throws std::runtime_error If a body placeholder cannot be filled by this pattern.
 * @note Validates supplied values only; does not modify the pattern or rule.
 */
void validatePattern(const Pattern& pattern, const Rule& rule);
/**
 * @brief Compile the phrase DSL into an optimized ECMAScript regex and capture map.
 *
 * Expands optional words and alternatives into noncapturing groups, converts
 * named slots into numbered captures, and escapes literal regex metacharacters.
 *
 * @param[in] phrase DSL source containing 1..512 bytes.
 * @return Compiled expression and the capture index for each named slot.
 * @throws std::runtime_error For invalid length, unknown/repeated slots, or malformed
 *                            optional sections.
 * @throws std::regex_error If the resulting expression cannot compile.
 */
Pattern phrasePattern(const string& phrase);
/**
 * @brief Validate a version-1 phrase configuration and compile its ordered rules.
 *
 * Checks root/rule fields, supported domain/service combinations, unique rule IDs,
 * body structure, alias shape, capture indexes, and placeholder compatibility.
 * Rule ordering is preserved. Installation-specific control policy is deliberately
 * validated separately by the application and executor.
 *
 * @param[in] config Decoded configuration with 1..256 rules and optional aliases.
 * @return Independently owned compiled rules ready for a candidate parser snapshot.
 * @throws std::runtime_error For an unsupported or inconsistent grammar definition.
 * @throws std::regex_error If a configured or generated expression is invalid.
 * @throws nlohmann::json::exception For missing mandatory fields or incompatible types.
 * @note Does not read configuration files or mutate an existing parser.
 */
std::vector<Rule> compileRules(const json& config);
/**
 * @brief Remove leading and trailing ASCII space characters from a string.
 *
 * @param[in] text String to inspect; tabs and other whitespace are not trimmed here.
 * @return Trimmed copy, or an empty string when the input contains only spaces.
 */
std::string trim(const std::string& text);
/**
 * @brief Normalize recognized speech while preserving decimal points and entity IDs.
 *
 * Lowercases bytes using the active C character classification, expands ampersands
 * into "and", converts hyphens/whitespace and selected punctuation to spaces,
 * removes trailing sentence dots, and collapses repeated whitespace.
 *
 * @param[in] input Raw transcript or device alias to normalize.
 * @return Normalized text with no leading/trailing spaces.
 * @note This is byte-oriented normalization, not full Unicode case folding.
 */
std::string normalize(const std::string& input);
/**
 * @brief Parse the strict spoken-number grammar into a normalized 0..1 level.
 *
 * Whole numbers mean percentages, while bare decimals up to one mean normalized
 * levels. Explicit percent suffixes always select percentages. Supports selected
 * English number words, decimal digit words, half, full, maximum, and minimum;
 * negative values, out-of-range levels, and trailing command text are rejected.
 *
 * @param[in] text Numeric phrase, copied for local trimming and suffix removal.
 * @param[out] value Normalized level when parsing succeeds; unspecified on failure.
 * @return True for a complete, finite value within 0..1; false for invalid input.
 */
bool level(std::string text, double& value);
/**
 * @brief Resolve a captured device name and distinguish failure from a permitted null target.
 *
 * Prefers domain-scoped discovery aliases, rejects blocked/ambiguous aliases, and
 * optionally permits one or two edits according to alias length. Missing media
 * names can succeed with a null target; other domains require a usable target.
 * Unresolved names are retained only for non-strict media parsing without aliases.
 *
 * @param[in] captured Spoken target, possibly empty when omitted by the phrase.
 * @param[in] domain Requested Home Assistant entity domain.
 * @param[in] options Supplied alias/state context and matching preferences.
 * @param[out] target Resolved string or null; null on rejected resolution.
 * @return True when target handling is permitted, including unresolved/null media
 *         targets; false for missing required, unknown, blocked, or ambiguous targets.
 * @note Does not fetch devices or modify options; executors validate final targets.
 */
bool resolveDevice(const string& captured, const string& domain, const Options& options, json& target);
} // namespace Parser::Detail
