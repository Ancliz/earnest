/**
 * @file
 * @brief Discovery filtering, normalized aliases, and collision handling.
 */

#include "device_aliases.hpp"

#include <algorithm>
#include <set>
#include <stdexcept>
#include <utility>

using Commands::json;
using std::runtime_error;
using std::string;
using std::unordered_map;
using std::vector;

vector<Device> findAvailableDevices(const json& states, bool includeUnavailable) {
    if(!states.is_array()) {
        throw runtime_error("Home Assistant states must be a JSON array");
    }

    static const std::set<string> domains = {"media_player", "climate", "light", "switch",
                                             "fan",          "cover",   "select"};
    vector<Device> devices;

    for(const auto& state : states) {
        if(!state.is_object() || !state.contains("entity_id") || !state["entity_id"].is_string()) {
            continue;
        }

        const string entity = state["entity_id"].get<string>();
        const auto dot = entity.find('.');
        const string domain = entity.substr(0, dot);

        if(dot == string::npos || dot + 1 == entity.size() || !domains.count(domain)) {
            continue;
        }

        const string status = state.value("state", string("unknown"));

        if(!includeUnavailable && (status == "unavailable" || status == "unknown")) {
            // Fixtures without a state field still describe aliases; explicit unavailable states do not.
            if(state.contains("state")) {
                continue;
            }
        }

        json attributes = state.value("attributes", json::object());

        if(!attributes.is_object()) {
            attributes = json::object();
        }

        const string name = attributes.contains("friendly_name") && attributes["friendly_name"].is_string()
                                ? attributes["friendly_name"].get<string>()
                                : entity.substr(dot + 1);
        devices.push_back({entity, name, domain, status, attributes});
    }

    return devices;
}

Commands::Context buildCommandContext(const json& states, Commands::Context options) {
    const auto devices = findAvailableDevices(states, true);

    if(devices.empty()) {
        throw runtime_error("Home Assistant returned no controllable entities");
    }

    unordered_map<string, unordered_map<string, string>> domains;
    unordered_map<string, json> stateMap;

    for(const auto& device : devices) {
        auto& aliases = domains[device.domain];
        /*
         * @brief Add one normalized name to the current domain's candidate alias map.
         *
         * @param[in] name Friendly name, full entity ID, or readable entity name to normalize.
         * @note Ignores empty names, preserves repeated aliases for the same entity, and marks
         *       a collision between different entities with a blocked empty ID. Mutates only
         *       the locally built map, not the caller's original context.
         */
        const auto addAlias = [&](const string& name) {
            const string alias = Commands::normalizeDeviceName(name);

            if(alias.empty()) {
                return;
            }

            const auto entry = aliases.emplace(alias, device.entityId);

            if(!entry.second && entry.first->second != device.entityId) {
                entry.first->second.clear();
            }
        };
        addAlias(device.entityId);
        string readable = device.entityId.substr(device.domain.size() + 1);
        std::replace(readable.begin(), readable.end(), '_', ' ');
        addAlias(readable);
        addAlias(device.friendlyName);
        stateMap[device.entityId] = {
            {"entity_id", device.entityId}, {"state", device.state}, {"attributes", device.attributes}};
    }

    options.devices =
        domains.count("media_player") ? domains.at("media_player") : unordered_map<string, string>();
    options.domainDevices = std::move(domains);
    options.deviceStates = std::move(stateMap);
    options.strictDevices = true;
    return options;
}