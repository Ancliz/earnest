/**
 * @file
 * @brief Regex parser backend interface and instance-owned grammar configuration.
 */

#pragma once

#include "commands/command.hpp"
#include <memory>

namespace Parser {
using std::string;
using std::unordered_map;
using json = Commands::json;
using Options = Commands::Context;

using Commands::normalizeDeviceName;
using Commands::resolveEntityId;

class RegexParser final : public Commands::Parser {
    struct Impl;
    std::unique_ptr<Impl> impl;

public:
    /**
     * @brief Construct an independent regex parser with no active rules.
     *
     * Allocate implementation state without reading configuration or discovering
     * any devices. Call configure() before expecting supported commands to match.
     */
    RegexParser();
    /**
     * @brief Construct a parser and compile a supplied configuration snapshot.
     *
     * @param[in] configuration Decoded phrase configuration containing version, rules,
     *                          and optional aliases; installation policy is not applied here.
     * @throws std::runtime_error If rule or alias validation fails.
     * @throws std::regex_error If a phrase or regex cannot be compiled.
     * @throws nlohmann::json::exception If configuration fields have incompatible types.
     * @note Does not read files, query devices, or execute commands.
     */
    explicit RegexParser(const json& configuration);
    /**
     * @brief Release this instance's compiled patterns and copied configuration.
     *
     * Other parser instances retain their own independent rule snapshots.
     */
    ~RegexParser() override;
    /**
     * @brief Disable copying of the uniquely owned compiled parser implementation.
     *
     * Construct and configure a separate instance when independent mutable rules are needed.
     */
    RegexParser(const RegexParser&) = delete;
    /**
     * @brief Disable copy assignment of independently owned parser state.
     *
     * Use configure() to explicitly replace this parser's rule snapshot.
     */
    RegexParser& operator=(const RegexParser&) = delete;

    /**
     * @brief Compile and commit a new phrase configuration for this instance.
     *
     * Builds candidate rules and copies configuration before swapping implementation
     * state. Existing compiled rules and in-memory additions survive failed attempts;
     * a successful replacement discards additions previously made with addPhrase().
     *
     * @param[in] configuration Complete decoded version-1 phrase and alias configuration.
     * @throws std::runtime_error If rule structure, aliases, slots, or services are invalid.
     * @throws std::regex_error If a candidate pattern is invalid.
     * @throws nlohmann::json::exception If candidate fields have incompatible types.
     * @note Does not validate installation control policy, read files, or perform HTTP.
     */
    void configure(const json& configuration) override;
    /**
     * @brief Append one compiled phrase to an existing rule in memory.
     *
     * Validates placeholder compatibility before modifying the matched rule. This
     * addition is local to the instance and is replaced by the next configure() call.
     *
     * @param[in] ruleId Exact identifier of an existing compiled rule.
     * @param[in] phrase Phrase DSL text, containing 1..512 bytes.
     * @throws std::runtime_error If the rule is unknown, the phrase is invalid, or a
     *                            required body placeholder has no corresponding slot.
     * @throws std::regex_error If the generated pattern cannot compile.
     * @note Does not persist the phrase or reload configuration from disk.
     */
    void addPhrase(const string& ruleId, const string& phrase);
    /**
     * @brief Parse one transcript with the active grammar and a supplied device snapshot.
     *
     * Normalizes speech, rejects negative/negated or unsafe compound input, overlays
     * configured aliases on a local context copy, and converts captured values into
     * typed command fields. Conflicting interpretations across domains return null.
     * Explicit relative-volume amounts become internal adjustment intents rather than
     * conversation requests. Omitted media targets may remain null for executor rejection.
     *
     * @param[in] input Completed transcript; empty input or more than 512 bytes is rejected.
     * @param[in] options Device context and resolution preferences, left unchanged.
     * @return One command object with endpoint/body, an internal volume-adjustment intent,
     *         or JSON null when no unambiguous supported interpretation exists.
     * @throws nlohmann::json::exception If supplied context or configured substitution
     *                                  values have incompatible types.
     * @note Performs no HTTP, file reads, reloads, logging, or device control. Configure
     *       and parse on one thread, or synchronize externally.
     */
    json parse(const string& input, const Options& options = {}) const override;
};

class OllamaParser final : public Commands::Parser {
    struct Impl;
    std::unique_ptr<Impl> impl;

    void configure(const json& configuration) override;
    
    json parse(const string& input, const Options& options = {}) const override;

};

} // namespace Parser