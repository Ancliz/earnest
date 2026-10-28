/**
 * @file
 * @brief Shared command context, parser/executor contracts, and execution results.
 */

#pragma once

#include <string>
#include <unordered_map>
#include <nlohmann/json.hpp>

using std::string;
using std::unordered_map;

namespace Commands {
using json = nlohmann::json;

struct Context {
    /// Maps normalized media-player names and aliases to their entity IDs.
    unordered_map<string, string> devices;

    /// Maps each domain to its normalized device names and aliases, each resolving to an entity ID.
    unordered_map<string, unordered_map<string, string>> domainDevices;

    /// Maps entity IDs to their current state and attributes as JSON.
    unordered_map<string, json> deviceStates;
    
    bool fuzzyDevices = true;
    bool strictDevices = false;
    string defaultDevice;
};

/**
 * @brief Produce the canonical spelling used for spoken device aliases.
 *
 * Applies speech normalization, removes one leading "the " and one trailing
 * possessive "'s", and trims surrounding spaces. Entity-ID dots and underscores
 * are preserved; this function does not query discovery or resolve an entity.
 *
 * @param[in] name Device name or alias, copied before normalization.
 * @return Canonical alias, possibly empty when the input contains no name.
 */
string normalizeDeviceName(string name);
/**
 * @brief Resolve a spoken name within one device domain using a supplied context.
 *
 * Uses normalized aliases, optional bounded fuzzy matching, and the configured
 * default target. Equally good matches to different entities and deliberately
 * blocked aliases are rejected. Non-strict media-player contexts may retain an
 * unresolved name; a missing media target can also resolve to null.
 *
 * @param[in] name Captured spoken name; an empty name requests default-target handling.
 * @param[in] domain Home Assistant domain, such as "climate" or "media_player".
 * @param[in] context Discovery snapshot and resolution preferences; never modified.
 * @return Entity-ID string, permitted unresolved media name, or JSON null when no
 *         usable target can be supplied.
 * @note Performs no HTTP, file access, logging, or device control.
 */
json resolveEntityId(const string& name, const string& domain, const Context& context);

class Parser {
public:
    /**
     * @brief Destroy a parser implementation through the shared interface.
     *
     * Derived destructors release backend-owned resources. Destruction must not
     * execute any pending device command.
     */
    virtual ~Parser() = default;
    /**
     * @brief Prepare and commit a configuration for this parser instance.
     *
     * Implementations must validate and prepare candidate state before replacing
     * active state. The application uses this guarantee to keep its policy snapshot
     * consistent with the parser when a configuration reload fails.
     *
     * @param[in] configuration Decoded application configuration; backend interpretation
     *                          is implementation-specific.
     * @throws std::exception If candidate validation or preparation fails; the previous
     *                       usable configuration must remain active.
     * @note May change parser-owned state, but must not execute device commands.
     */
    virtual void configure(const json& configuration) = 0;
    /**
     * @brief Interpret completed speech or typed text as one proposed command.
     *
     * The parser receives data, rather than a device-control client. Backends may
     * use local grammar or an inference service, but execution belongs to Executor.
     * The current command contract represents a single request, not a batch of tools.
     *
     * @param[in] text Completed transcript or user-entered command text.
     * @param[in] context Discovered devices, aliases, state, and resolution preferences.
     * @return Proposed command JSON, or null for unsupported, invalid, or ambiguous input.
     * @throws std::exception If a backend encounters an operational or invalid-data error.
     * @note Must not execute commands or change the supplied context. A returned command
     *       still requires independent executor validation.
     */
    virtual json parse(const string& text, const Context& context = {}) const = 0;
};

struct ExecutionResult {
    bool success = false;
    string error;
    json request = nullptr;
};

class Executor {
public:
    /**
     * @brief Release executor-owned resources through the polymorphic interface.
     *
     * Destruction must not retry or execute previously submitted commands.
     */
    virtual ~Executor() = default;
    /**
     * @brief Resolve and validate a proposed command without controlling a device.
     *
     * An implementation may read fresh state or service metadata. It must apply the
     * same acceptance rules used immediately before execution, including rules that
     * are independent of the parser that supplied the command.
     *
     * @param[in] command Proposed command or internal intent produced by a parser.
     * @param[in] configuration Active application policy snapshot.
     * @return Validation outcome, explanatory error on failure, and a resolved request
     *         on success. A successful check does not reserve state for later execution.
     * @note Read-only network access is permitted; control requests are forbidden.
     */
    virtual ExecutionResult check(const json& command, const json& configuration) = 0;
    /**
     * @brief Validate and execute one proposed command using the active policy.
     *
     * Execution must validate current conditions rather than trusting an earlier
     * preview or relying on the parser implementation to have vetted its output.
     *
     * @param[in] command Proposed command or internal intent to execute.
     * @param[in] configuration Active application policy snapshot.
     * @return Execution outcome and error details; successful implementations may also
     *         return the concrete request they submitted.
     * @note This operation may change external device state. Backend retry and transport
     *       semantics are specified by the concrete executor.
     */
    virtual ExecutionResult execute(const json& command, const json& configuration) = 0;
};
} // namespace Commands
