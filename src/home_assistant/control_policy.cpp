#include "control_policy.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <stdexcept>

using Parser::json;
using std::string;

namespace {

const json* findState(const json& states, const string& entity) {
    for(const auto& state : states) {
        if(state.is_object() && state.value("entity_id", string()) == entity) {
            return &state;
        }
    }

    return nullptr;
}

bool member(const json& list, const string& value) {
    return list.is_array() && std::find(list.begin(), list.end(), value) != list.end();
}

bool isOpen(const json* state) {
    if(!state) {
        return false;
    }

    const string status = state->value("state", string());
    const string entity = state->value("entity_id", string());

    if(entity.compare(0, 8, "climate.") == 0) {
        return status != "off" && status != "unknown" && status != "unavailable";
    }

    return state->contains("attributes") && (*state)["attributes"].contains("current_position") &&
           (*state)["attributes"]["current_position"].is_number() &&
           (*state)["attributes"]["current_position"].get<double>() > 0;
}

} // namespace

void validateControlConfiguration(const json& configuration) {
    if(!configuration.contains("advantage_air")) {
        return;
    }

    const auto& policy = configuration.at("advantage_air");

    if(!policy.is_object() || !policy.contains("systems") || !policy["systems"].is_array()) {
        throw std::runtime_error("advantage_air requires a systems list");
    }

    std::set<string> entities;

    for(const auto& system : policy["systems"]) {
        if(!system.is_object() || !system.contains("entity") || !system["entity"].is_string() ||
           !system.contains("zones") || !system["zones"].is_array() || system["zones"].empty() ||
           !system.contains("constant_zones") || !system["constant_zones"].is_array() ||
           system["constant_zones"].empty() || !system.contains("minimum_open_zones") ||
           !system["minimum_open_zones"].is_number_integer() || !system.contains("verified_preset") ||
           !system["verified_preset"].is_string()) {
            throw std::runtime_error(
                "AdvantageAir systems need an entity, zones, constant_zones, and minimum_open_zones");
        }

        const string main = system["entity"].get<string>();
        const int minimum = system["minimum_open_zones"].get<int>();

        if(main.compare(0, 8, "climate.") != 0 || !entities.insert(main).second || minimum < 1 ||
           minimum > static_cast<int>(system["zones"].size())) {
            throw std::runtime_error("Invalid AdvantageAir main entity or minimum zone count");
        }

        for(const auto& zone : system["zones"]) {
            if(!zone.is_string() || !entities.insert(zone.get<string>()).second) {
                throw std::runtime_error("AdvantageAir zones must be unique entity IDs");
            }
        }

        for(const auto& zone : system["constant_zones"]) {
            if(!zone.is_string() || !member(system["zones"], zone.get<string>())) {
                throw std::runtime_error("Every constant zone must be in zones");
            }
        }
    }
}

string validateCommand(const json& request, const json& states, const json& configuration) {
    if(!request.is_object() || !request.contains("endpoint") || !request["endpoint"].is_string() ||
       !request.contains("body") || !request["body"].is_object()) {
        return "Invalid request";
    }

    const string endpoint = request["endpoint"].get<string>();
    const auto& body = request["body"];

    // The parser only delegates validated media-volume deltas, never HVAC or installer commands.
    if(endpoint == "/conversation/process") {
        return "";
    }

    if(!body.contains("entity_id") || !body["entity_id"].is_string()) {
        return "Choose a device first";
    }

    const string entity = body["entity_id"].get<string>();
    const auto* state = findState(states, entity);

    if(!state) {
        return "Device no longer exists: " + entity;
    }

    const string status = state->value("state", string());

    if(status == "unavailable" || status == "unknown") {
        return "Device is unavailable: " + entity;
    }

    const auto& attributes = state->at("attributes");
    const string domain = entity.substr(0, entity.find('.'));
    const string expected = "/services/" + domain + "/";

    if(endpoint.compare(0, expected.size(), expected) != 0) {
        return "Service domain does not match the device";
    }

    const string service = endpoint.substr(expected.size());
    static const std::map<string, std::set<string>> fields = {
        {"turn_on", {"brightness_pct"}},
        {"turn_off", {}},
        {"volume_set", {"volume_level"}},
        {"volume_up", {}},
        {"volume_down", {}},
        {"volume_mute", {"is_volume_muted"}},
        {"media_play", {}},
        {"media_pause", {}},
        {"media_stop", {}},
        {"media_play_pause", {}},
        {"media_next_track", {}},
        {"media_previous_track", {}},
        {"set_temperature", {"temperature"}},
        {"set_hvac_mode", {"hvac_mode"}},
        {"set_fan_mode", {"fan_mode"}},
        {"set_percentage", {"percentage"}},
        {"open_cover", {}},
        {"close_cover", {}},
        {"set_cover_position", {"position"}},
        {"select_option", {"option"}}
    };

    if(!fields.count(service)) {
        return "Unsupported service";
    }

    for(const auto& field : body.items()) {
        if(field.key() != "entity_id" && !fields.at(service).count(field.key())) {
            return "Unexpected service field: " + field.key();
        }
    }

    for(const string field : {"volume_level", "brightness_pct", "percentage", "position", "temperature"}) {
        if(!body.contains(field)) {
            continue;
        }

        if(!body[field].is_number() || !std::isfinite(body[field].get<double>())) {
            return "Invalid numeric field: " + field;
        }

        const double value = body[field].get<double>();
        const double maximum = field == "volume_level" ? 1 : 100;

        if(value < 0 || value > maximum) {
            return "Value is out of range: " + field;
        }
    }

    if(service == "volume_set" && !body.contains("volume_level")) {
        return "Missing volume_level";
    }

    if(service == "volume_mute" &&
       (!body.contains("is_volume_muted") || !body["is_volume_muted"].is_boolean())) {
        return "Missing mute boolean";
    }

    if(domain == "climate") {
        const auto supported = attributes.value("supported_features", 0);

        if(service == "turn_on" && !(supported & 256)) {
            return "This climate entity cannot be turned on";
        }

        if(service == "turn_off" && !(supported & 128)) {
            return "This climate entity cannot be turned off";
        }

        if(service == "set_temperature") {
            if(!(supported & 1)) {
                return "This climate entity does not support a single temperature setpoint in its current preset";
            }

            if(!body.contains("temperature") || !attributes.contains("min_temp") ||
               !attributes.contains("max_temp")) {
                return "Temperature limits are unavailable";
            }

            const double value = body["temperature"].get<double>();
            const double min = attributes["min_temp"].get<double>();
            const double max = attributes["max_temp"].get<double>();
            const double step = attributes.value("target_temp_step", 1.0);

            if(value < min || value > max || step <= 0 ||
               std::abs((value - min) / step - std::round((value - min) / step)) > 1e-6) {
                return "Temperature is outside the device range or step";
            }
        }

        for(const auto& pair :
            {std::make_pair("hvac_mode", "hvac_modes"), std::make_pair("fan_mode", "fan_modes")}) {
            if(service != string("set_") + pair.first) {
                continue;
            }

            if(!body.contains(pair.first) || !body[pair.first].is_string() ||
               !attributes.contains(pair.second) ||
               !member(attributes[pair.second], body[pair.first].get<string>())) {
                return "Unsupported climate mode";
            }
        }

        if(service == "set_fan_mode" && !(supported & 8)) {
            return "Fan speed is not supported by this climate entity";
        }
    }

    if(domain == "select" &&
       (!body.contains("option") || !body["option"].is_string() || !attributes.contains("options") ||
        !member(attributes["options"], body["option"].get<string>()))) {
        return "Unsupported select option";
    }

    if(domain == "cover" && attributes.value("device_class", string()) == "damper") {
        if(body.contains("position") && std::fmod(body["position"].get<double>(), 5) != 0) {
            return "Damper position must use 5 percent increments";
        }
    }

    for(const auto& system :
        configuration.value("advantage_air", json::object()).value("systems", json::array())) {
        if(domain == "select" && entity == system.value("myzone_entity", string())) {
            return "Change MyZone on the controller; voice does not change the installation's constant-zone "
                   "selection";
        }
    }

    // Require an explicit, verified installation policy for every climate/damper command.
    // This avoids treating a zone's heat_cool state as the compressor's operating mode.
    if(domain == "climate" || (domain == "cover" && attributes.value("device_class", string()) == "damper")) {
        const json systems =
            configuration.value("advantage_air", json::object()).value("systems", json::array());
        bool known = false;

        for(const auto& system : systems) {
            const string main = system["entity"].get<string>();
            const bool mainTarget = entity == main;

            if(!mainTarget && !member(system["zones"], entity)) {
                continue;
            }

            known = true;
            const auto* master = findState(states, main);

            if(!master || master->value("state", string()) == "unavailable" ||
               master->value("state", string()) == "unknown") {
                return "AC system state is unavailable";
            }

            const bool running = master->value("state", string()) != "off";
            const bool closing = service == "turn_off" || service == "close_cover" ||
                                 (body.contains("hvac_mode") && body["hvac_mode"] == "off") ||
                                 (body.contains("position") && body["position"] == 0);
            const bool starting =
                mainTarget && (service == "turn_on" || (service == "set_hvac_mode" && !closing) ||
                               (running && service == "set_fan_mode"));

            if(mainTarget && closing) {
                return "";
            }

            if(!starting && !(running && !mainTarget && closing)) {
                break;
            }

            if(master->at("attributes").value("preset_mode", string()) !=
               system.value("verified_preset", string())) {
                return "AC preset changed; review the verified constant-zone policy before changing airflow";
            }

            unsigned open = 0;

            for(const auto& zone : system["zones"]) {
                const string id = zone.get<string>();
                const auto* current = findState(states, id);

                if(!current || current->value("state", string()) == "unavailable" ||
                   current->value("state", string()) == "unknown") {
                    return "Cannot verify every AC zone state";
                }

                if(isOpen(current) && !(id == entity && closing)) {
                    ++open;
                }
            }

            if(open < system["minimum_open_zones"].get<unsigned>()) {
                return "Command would leave too few AC zones open";
            }

            for(const auto& zone : system["constant_zones"]) {
                const string id = zone.get<string>();

                if(!isOpen(findState(states, id)) || (id == entity && closing)) {
                    return "Keep the installer-configured constant zone open before running the AC";
                }
            }
        }

        if(!known) {
            return "No verified airflow policy is configured for this climate entity";
        }
    }

    return "";
}