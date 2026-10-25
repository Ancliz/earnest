#include "device_aliases.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

using std::runtime_error;
using std::string;
using std::unordered_map;
using Parser::json;

void populateDeviceAliases(const json& states, Parser::Options& options) {
    if(!states.is_array()) {
        throw runtime_error("Home Assistant states must be a JSON array");
    }

    unordered_map<string, string> aliases;

    const auto addAlias = [&](const string& name, const string& entity) {
        const string alias = Parser::normalizeDeviceName(name);

        if(alias.empty()) {
            return;
        }

        const auto entry = aliases.emplace(alias, entity);

        if(!entry.second && entry.first->second != entity) {
            // An empty destination blocks exact and fuzzy matches to ambiguous aliases
            entry.first->second.clear();
        }
    };

    for(const auto& state : states) {
        if(!state.is_object() || !state.contains("entity_id") || !state["entity_id"].is_string()) {
            continue;
        }

        const string entity = state["entity_id"].get<string>();

        if(entity.compare(0, 13, "media_player.") != 0 || entity.size() == 13) {
            continue;
        }

        addAlias(entity, entity);
        string objectName = entity.substr(13);
        std::replace(objectName.begin(), objectName.end(), '_', ' ');
        addAlias(objectName, entity);

        if(state.contains("attributes") && state["attributes"].is_object()) {
            const auto& attributes = state["attributes"];

            if(attributes.contains("friendly_name") && attributes["friendly_name"].is_string()) {
                addAlias(attributes["friendly_name"].get<string>(), entity);
            }
        }
    }

    if(aliases.empty()) {
        throw runtime_error("Home Assistant returned no media-player entities");
    }

    options.devices = std::move(aliases);
}
