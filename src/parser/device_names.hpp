/**
 * @file
 * @brief Spoken device-name lookup with alias collisions and bounded fuzzy matching.
 */

#pragma once

#include "commands/command.hpp"
#include <optional>
#include <unordered_map>

namespace Grammar {

/// JSON value type shared with the command contracts.
using Commands::json;
using std::string;

/**
 * @brief Levenshtein distance between two names, giving up once it must exceed a limit.
 * @return Exact distance when within limit, otherwise some value above limit.
 */
std::size_t editDistance(const string& a, const string& b, std::size_t limit);

/**
 * @brief Spoken-name index for one device snapshot.
 *
 * Each device is known by its entity ID, its object ID with spaces ("living_room" becomes
 * "living room"), its friendly name, and any configured aliases. A name claimed by two
 * different entities in one domain is blocked rather than guessed.
 */
class DeviceNames {
    /**
     * @brief Normalized names to entity IDs; an empty entity marks a blocked name.
     */
    using NameMap = std::unordered_map<string, string>;

    /// Name maps by domain.
    std::unordered_map<string, NameMap> domains;

    /// Entity used when no name is spoken.
    string defaultDevice;
    
    /// Whether misspelled names may match.
    bool fuzzy;

    /**
     * @brief Register one spoken name, blocking it if another entity already claimed it.
     */
    void add(const string& domain, const string& name, const string& entity);

public:
    /**
     * @brief Index the devices in a context.
     * @param[in] context Devices and resolution preferences.
     * @param[in] aliases Configured spoken names by entity ID; aliases of absent devices are ignored.
     */
    DeviceNames(const Commands::Context& context, const json& aliases);

    /**
     * @brief Resolve a spoken device name within one domain.
     *
     * An empty name uses the default device when it belongs to the domain. Otherwise media
     * commands get an empty entity, for the executor to reject, and other domains fail.
     *
     * @return Entity ID, empty for an unnamed media target, or nullopt when the name is unknown,
     *         blocked, ambiguous, or looks like more than one command.
     */
    std::optional<string> resolve(const string& spoken, const string& domain) const;
};
} // namespace Grammar