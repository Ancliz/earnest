/**
 * @file
 * @brief Home Assistant HTTP transport, discovery, and checked execution.
 */

#include "home_assistant.hpp"

#include <stdexcept>

using Commands::Command;
using Commands::Devices;
using Commands::json;
using Commands::Result;
using std::runtime_error;
using std::string;

namespace {
/**
 * @brief Domains voice may not control unless voice_control.allow names them.
 *
 * Security devices, system administration, notifications and assistants, automations and
 * scripts (which can do anything), and helper or configuration entities that are not devices.
 */
const std::set<string> defaultExclusions = {
    "alarm_control_panel", "camera", "lock", "siren",
    "backup", "cloud", "ffmpeg", "file", "frontend", "homeassistant", "logbook", "logger", "recorder", "system_log", "update",
    "ai_task", "assist_satellite", "conversation", "notify", "persistent_notification", "tts",
    "automation", "counter", "schedule", "script", "timer",
    "button", "date", "datetime", "device_tracker", "image", "image_processing", "input_boolean", "input_button",
    "input_datetime", "input_number", "input_select", "input_text", "number", "person", "shopping_list", "text",
    "time", "todo", "weather", "zone"};

/**
 * @brief Read a list of domain or domain.service names from voice_control.
 * @throws std::runtime_error If the entry is not a list of strings.
 */
std::set<string> readNames(const json& section, const char* key) {
    std::set<string> names;

    if(!section.contains(key)) {
        return names;
    }

    if(!section[key].is_array()) {
        throw runtime_error(string("voice_control.") + key + " must be a list");
    }

    for(const auto& name : section[key]) {
        if(!name.is_string() || name.get<string>().empty()) {
            throw runtime_error(string("voice_control.") + key + " must list domain or domain.service names");
        }

        names.insert(name.get<string>());
    }

    return names;
}

/**
 * @brief Add a service's fields, descending into collapsible sections that group further fields.
 */
void addFields(const json& fields, Commands::ServiceFields& entry) {
    if(!fields.is_object()) {
        return;
    }

    for(const auto& [name, field] : fields.items()) {
        if(!field.is_object()) {
            continue;
        } else if(field.contains("fields")) {
            addFields(field["fields"], entry);
            continue;
        }

        entry.allowed.insert(name);

        if(field.value("required", false)) {
            entry.required.insert(name);
        }

        if(field.contains("selector")) {
            entry.selectors[name] = field["selector"];
        }
    }
}
} // namespace

bool VoiceScope::allows(const string& domain, const string& service) const {
    return !excluded.count(domain) && !excluded.count(domain + "." + service);
}

VoiceScope readVoiceScope(const json& configuration) {
    VoiceScope scope{defaultExclusions};
    const json section = configuration.is_object() ? configuration.value("voice_control", json::object()) : json();

    if(!section.is_object()) {
        throw runtime_error("voice_control must be a mapping");
    }

    for(const auto& name : readNames(section, "allow")) {
        scope.excluded.erase(name);
    }

    for(const auto& name : readNames(section, "exclude")) {
        scope.excluded.insert(name);
    }

    return scope;
}

ServiceCatalogue readServiceCatalogue(const json& services, const VoiceScope& scope) {
    if(!services.is_array()) {
        throw runtime_error("Home Assistant services must be a JSON array");
    }

    ServiceCatalogue catalogue;

    for(const auto& group : services) {
        const json domain = group.is_object() ? group.value("domain", json()) : json();

        if(!domain.is_string() || !group.contains("services") || !group["services"].is_object()) {
            continue;
        }

        for(const auto& [name, service] : group["services"].items()) {
            // Services without an entity target (restart, notify, reload) are not device commands.
            const bool targetsEntities = service.is_object() && service.contains("target") && service["target"].is_object() && service["target"].contains("entity");

            if(!targetsEntities || !scope.allows(domain.get<string>(), name)) {
                continue;
            }

            auto& entry = catalogue[domain.get<string>()][name];
            addFields(service.value("fields", json::object()), entry);
        }
    }

    // Relative volume is resolved into volume_set during validation, so it needs no Home Assistant field.
    for(const char* step : {"volume_up", "volume_down"}) {
        if(findService(catalogue, "media_player", step)) {
            auto& entry = catalogue["media_player"][step];
            entry.allowed.insert("amount");
            entry.selectors["amount"] = {{"number", {{"min", 0}, {"max", 1}}}};
        }
    }

    return catalogue;
}

Devices controllableDevices(const json& states, const ServiceCatalogue& services) {
    if(!states.is_array()) {
        throw runtime_error("Home Assistant states must be a JSON array");
    }

    Devices devices;

    for(const auto& state : states) {
        const json entityId = state.is_object() ? state.value("entity_id", json()) : json();

        if(!entityId.is_string()) {
            continue;
        }

        const string entity = entityId.get<string>();
        const auto dot = entity.find('.');

        // A domain is controllable while Home Assistant offers it services that voice may use.
        if(dot == string::npos || dot + 1 == entity.size() || !services.count(entity.substr(0, dot))) {
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

bool HomeAssistant::refreshServices(bool force) {
    const auto age = Clock::now() - fetched;

    if(age < (force ? refetchInterval : catalogueLifetime)) {
        return false;
    }

    services = readServiceCatalogue(getArray("/api/services"), scope);
    fetched = Clock::now();
    return true;
}

void HomeAssistant::configure(const json& configuration) {
    auto nextPolicy = readAirflowPolicy(configuration);
    scope = readVoiceScope(configuration);
    policy = std::move(nextPolicy);
    fetched = {};
}

Devices HomeAssistant::discoverDevices() {
    refreshServices();
    return controllableDevices(getArray("/api/states"), services);
}

ServiceCatalogue HomeAssistant::discoverServices() {
    refreshServices();
    return services;
}

Result HomeAssistant::preview(const Command& command) {
    try {
        auto result = validateCommand(command, discoverDevices(), services, policy);

        // A service the cached catalogue lacks may belong to a newly added integration; look again once.
        if(!result.success && !findService(services, result.command.domain, result.command.service) && refreshServices(true)) {
            result = validateCommand(command, discoverDevices(), services, policy);
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