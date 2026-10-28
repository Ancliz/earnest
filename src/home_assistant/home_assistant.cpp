/**
 * @file
 * @brief Home Assistant HTTP transport, discovery, and checked execution.
 */

#include "home_assistant.hpp"

#include <algorithm>
#include <set>
#include <stdexcept>

using Commands::Command;
using Commands::Devices;
using Commands::json;
using Commands::Result;
using std::runtime_error;
using std::string;

namespace {
/**
 * @brief Whether a /api/services array currently offers the command's service.
 *
 * Integrations can be unloaded at runtime, so the static catalog alone is not enough.
 */
bool isAdvertised(const json& services, const Command& command) {
    return std::any_of(services.begin(), services.end(), [&](const json& group) {
        return group.is_object() && group.value("domain", string()) == command.domain &&
               group.contains("services") && group["services"].is_object() &&
               group["services"].contains(command.service);
    });
}
} // namespace

Devices controllableDevices(const json& states) {
    if(!states.is_array()) {
        throw runtime_error("Home Assistant states must be a JSON array");
    }

    static const std::set<string> domains = {"media_player", "climate", "light", "switch",
                                             "fan",          "cover",   "select"};
    Devices devices;

    for(const auto& state : states) {
        const json entityId = state.is_object() ? state.value("entity_id", json()) : json();

        if(!entityId.is_string()) {
            continue;
        }

        const string entity = entityId.get<string>();
        const auto dot = entity.find('.');

        if(dot == string::npos || dot + 1 == entity.size() || !domains.count(entity.substr(0, dot))) {
            continue;
        }

        json attributes = state.value("attributes", json::object());

        if(!attributes.is_object()) {
            attributes = json::object();
        }

        const json friendlyName = attributes.value("friendly_name", json());
        const string name = friendlyName.is_string() ? friendlyName.get<string>() : entity.substr(dot + 1);
        devices.push_back({entity, name, entity.substr(0, dot), state.value("state", string("unknown")),
                           std::move(attributes)});
    }

    return devices;
}

HomeAssistant::HomeAssistant(const string& url, const string& token) : client(url) {
    client.set_bearer_token_auth(token);
    client.set_connection_timeout(5);
    client.set_read_timeout(10);
    client.set_write_timeout(10);
}

json HomeAssistant::getArray(const string& path) {
    const auto response = client.Get(path);

    if(!response) {
        throw runtime_error("Cannot query Home Assistant " + path + ": " +
                            httplib::to_string(response.error()));
    }

    if(response->status != 200) {
        throw runtime_error("Home Assistant " + path + " returned HTTP " + std::to_string(response->status));
    }

    auto body = json::parse(response->body, nullptr, false);

    if(!body.is_array()) {
        throw runtime_error("Home Assistant " + path + " returned invalid JSON");
    }

    return body;
}

void HomeAssistant::configure(const json& configuration) {
    policy = readAirflowPolicy(configuration);
}

Devices HomeAssistant::discoverDevices() {
    return controllableDevices(getArray("/api/states"));
}

Result HomeAssistant::preview(const Command& command) {
    try {
        auto result = validateCommand(command, discoverDevices(), policy);

        if(result.success && !isAdvertised(getArray("/api/services"), result.command)) {
            return {false, "Service is unavailable: " + result.command.domain + "." + result.command.service,
                    result.command};
        }

        return result;
    } catch(const std::exception& error) {
        return {false, string("Cannot verify command: ") + error.what(), command};
    }
}

Result HomeAssistant::execute(const Command& command) {
    auto result = preview(command);

    if(!result.success) {
        return result;
    }

    try {
        json body = result.command.data;
        body["entity_id"] = result.command.entity;
        const string path = "/api/services/" + result.command.domain + "/" + result.command.service;
        const auto response = client.Post(path, body.dump(), "application/json");

        if(!response) {
            return {false, "Home Assistant request failed: " + httplib::to_string(response.error()),
                    result.command};
        }

        if(response->status < 200 || response->status >= 300) {
            return {false,
                    "Home Assistant returned HTTP " + std::to_string(response->status) + ": " +
                        response->body,
                    result.command};
        }

        return result;
    } catch(const std::exception& error) {
        return {false, string("Home Assistant request failed: ") + error.what(), result.command};
    }
}