/**
 * @file
 * @brief Service, capability and AC airflow checks against an explicit device snapshot.
 */

#include "control_policy.hpp"
#include "commands/services.hpp"

#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>

using Commands::Command;
using Commands::Device;
using Commands::Devices;
using Commands::json;
using Commands::ServiceCatalogue;
using Commands::findService;
using std::runtime_error;
using std::string;
using std::vector;

namespace {
/**
 * @brief Home Assistant ClimateEntityFeature bits used by the climate checks.
 */
enum ClimateFeature { TargetTemperature = 1, FanMode = 8, TurnOff = 128, TurnOn = 256 };

/**
 * @brief Find a device by entity ID.
 * @return Borrowed pointer into the snapshot, or nullptr when absent.
 */
const Device* findDevice(const Devices& devices, const string& entity) {
    const auto found = std::find_if(devices.begin(), devices.end(),
                                    [&](const Device& device) { return device.entityId == entity; });
    return found == devices.end() ? nullptr : &*found;
}

/**
 * @brief Whether a device is absent or cannot currently report its state.
 */
bool isUnavailable(const Device* device) {
    return !device || device->state == "unavailable" || device->state == "unknown";
}

/**
 * @brief Whether a list contains a value.
 */
bool contains(const vector<string>& list, const string& value) {
    return std::find(list.begin(), list.end(), value) != list.end();
}

/**
 * @brief Whether a device attribute lists a string option, such as an hvac mode.
 */
bool listsOption(const json& attributes, const string& key, const json& option) {
    if(!option.is_string() || !attributes.contains(key) || !attributes[key].is_array()) {
        return false;
    }

    return std::find(attributes[key].begin(), attributes[key].end(), option) != attributes[key].end();
}

/**
 * @brief Whether a zone counts as open for airflow: a climate zone that is on, or a damper above 0%.
 */
bool isOpen(const Device* zone) {
    if(!zone) {
        return false;
    }

    if(zone->domain == "climate") {
        return zone->state != "off" && zone->state != "unknown" && zone->state != "unavailable";
    }

    const json position = zone->attributes.value("current_position", json());
    return position.is_number() && position.get<double>() > 0;
}

/**
 * @brief Whether a command closes its target: switching it off, closing it, or setting 0%.
 */
bool isClosing(const Command& command) {
    return command.service == "turn_off" || command.service == "close_cover" ||
           command.data.value("hvac_mode", json()) == "off" || command.data.value("position", json()) == 0;
}

/// Volume change for "turn it up" or "down" when no amount is said.
constexpr double defaultVolumeStep = 0.02;

/**
 * @brief Turn volume_up/volume_down into volume_set from the speaker's current level.
 * @return Rejection reason, or empty when resolved or not a relative command.
 */
string resolveRelativeVolume(Command& command, const Devices& devices) {
    const bool relative = command.domain == "media_player" && (command.service == "volume_up" || command.service == "volume_down");

    if(!relative) {
        return "";
    }

    const json amount = command.data.value("amount", json(defaultVolumeStep));

    if(command.data.size() > 1 || !amount.is_number()) {
        return "Invalid relative-volume command";
    }

    const double change = amount.get<double>();
    const Device* speaker = findDevice(devices, command.entity);

    if(!std::isfinite(change) || change < 0 || change > 1 || !speaker) {
        return "Cannot resolve relative-volume command";
    }

    const json current = speaker->attributes.value("volume_level", json());

    if(!current.is_number()) {
        return "Current volume is unavailable";
    }

    const double level = current.get<double>();

    if(!std::isfinite(level) || level < 0 || level > 1) {
        return "Current volume is invalid";
    }

    const double target = level + (command.service == "volume_up" ? change : -change);
    command.service = "volume_set";
    command.data = {{"volume_level", std::clamp(target, 0.0, 1.0)}};
    return "";
}

/**
 * @brief Check data fields against the service's discovered fields and their selectors.
 *
 * Number selectors bound values (levels 0..1, percentages 0..100); temperature limits come from
 * the device instead, as the service's own range is far wider than any device's.
 * @return Rejection reason, or empty when the fields are acceptable.
 */
string checkFields(const Command& command, const Commands::ServiceFields& fields) {
    for(const auto& field : command.data.items()) {
        if(!fields.allowed.count(field.key())) {
            return "Unexpected service field: " + field.key();
        }
    }

    for(const auto& field : fields.required) {
        if(!command.data.contains(field)) {
            return "Missing service field: " + field;
        }
    }

    for(const auto& [field, selector] : fields.selectors) {
        if(!command.data.contains(field) || !selector.is_object()) {
            continue;
        }

        const json& value = command.data.at(field);

        if(selector.contains("boolean") && !value.is_boolean()) {
            return "Expected true or false: " + field;
        } else if(!selector.contains("number")) {
            continue;
        } else if(!value.is_number() || !std::isfinite(value.get<double>())) {
            return "Invalid numeric field: " + field;
        }

        const json& range = selector["number"];
        const double number = value.get<double>();
        const bool belowMin = range.is_object() && range.value("min", json()).is_number() && number < range["min"].get<double>();
        const bool aboveMax = range.is_object() && range.value("max", json()).is_number() && number > range["max"].get<double>();

        if(belowMin || aboveMax) {
            return "Value is out of range: " + field;
        }
    }

    return "";
}

/**
 * @brief Check a climate command against the entity's supported features, limits and modes.
 * @return Rejection reason, or empty when supported.
 */
string checkClimate(const Command& command, const json& attributes) {
    const int features = attributes.value("supported_features", 0);
    const string& service = command.service;

    if(service == "turn_on" && !(features & TurnOn)) {
        return "This climate entity cannot be turned on";
    }

    if(service == "turn_off" && !(features & TurnOff)) {
        return "This climate entity cannot be turned off";
    }

    if(service == "set_temperature") {
        if(!(features & TargetTemperature)) {
            return "This climate entity does not support a single temperature setpoint in its current preset";
        }

        if(!attributes.contains("min_temp") || !attributes.contains("max_temp")) {
            return "Temperature limits are unavailable";
        }

        const double value = command.data.at("temperature").get<double>();
        const double min = attributes["min_temp"].get<double>();
        const double max = attributes["max_temp"].get<double>();
        const double step = attributes.value("target_temp_step", 1.0);
        const double steps = (value - min) / step;

        if(!std::isfinite(min) || !std::isfinite(max) || !std::isfinite(step) || min > max || value < min ||
           value > max || step <= 0 || std::abs(steps - std::round(steps)) > 1e-6) {
            return "Temperature is outside the device range or step";
        }
    }

    if((service == "set_hvac_mode" && !listsOption(attributes, "hvac_modes", command.data.at("hvac_mode"))) ||
       (service == "set_fan_mode" && !listsOption(attributes, "fan_modes", command.data.at("fan_mode")))) {
        return "Unsupported climate mode";
    }

    if(service == "set_fan_mode" && !(features & FanMode)) {
        return "Fan speed is not supported by this climate entity";
    }

    return "";
}

/**
 * @brief Apply the verified AdvantageAir policy to a climate or damper command.
 *
 * Starting the main unit, or closing a zone while it runs, must leave at least the minimum
 * number of zones, including every constant zone, open. Turning the main unit off is always
 * allowed.
 *
 * @return Rejection reason, or empty when the airflow stays within policy.
 */
string checkAirflow(const Command& command, const Devices& devices, const AirflowPolicy& policy) {
    const auto system = std::find_if(policy.begin(), policy.end(), [&](const AirflowSystem& candidate) {
        return candidate.entity == command.entity || contains(candidate.zones, command.entity);
    });

    if(system == policy.end()) {
        return "No verified airflow policy is configured for this climate entity";
    }

    const Device* mainUnit = findDevice(devices, system->entity);

    if(isUnavailable(mainUnit)) {
        return "AC system state is unavailable";
    }

    const bool targetsMainUnit = command.entity == system->entity;
    const bool running = mainUnit->state != "off";
    const bool closing = isClosing(command);
    const bool starting = command.service == "turn_on" || (command.service == "set_hvac_mode" && !closing) ||
                          (running && command.service == "set_fan_mode");

    // Only starting the main unit, or closing a zone while it runs, can starve the airflow.
    if(targetsMainUnit ? !starting : !(running && closing)) {
        return "";
    }

    if(mainUnit->attributes.value("preset_mode", string()) != system->verifiedPreset) {
        return "AC preset changed; review the verified constant-zone policy before changing airflow";
    }

    unsigned openZones = 0;

    // Count zones that stay open after this command, failing closed on any unknown zone.
    for(const auto& zone : system->zones) {
        const Device* state = findDevice(devices, zone);

        if(isUnavailable(state)) {
            return "Cannot verify every AC zone state";
        }

        if(isOpen(state) && !(zone == command.entity && closing)) {
            ++openZones;
        }
    }

    if(openZones < system->minimumOpenZones) {
        return "Command would leave too few AC zones open";
    }

    for(const auto& zone : system->constantZones) {
        if(!isOpen(findDevice(devices, zone)) || (zone == command.entity && closing)) {
            return "Keep the installer-configured constant zone open before running the AC";
        }
    }

    return "";
}

/**
 * @brief Validate a command whose internal intents are already resolved.
 * @return Rejection reason, or empty when accepted.
 */
string checkCommand(const Command& command, const Devices& devices, const ServiceCatalogue& services, const AirflowPolicy& policy) {
    if(!command.data.is_object()) {
        return "Invalid request";
    }

    if(command.entity.empty()) {
        return "Choose a device first";
    }

    const Device* device = findDevice(devices, command.entity);

    if(!device) {
        return "Device no longer exists: " + command.entity;
    }

    if(isUnavailable(device)) {
        return "Device is unavailable: " + command.entity;
    }

    if(device->domain != command.domain) {
        return "Service domain does not match the device";
    }

    const auto* fields = findService(services, command.domain, command.service);

    if(!fields) {
        return "Service is unavailable: " + command.domain + "." + command.service;
    }

    string problem = checkFields(command, *fields);

    if(problem.empty() && command.domain == "climate") {
        problem = checkClimate(command, device->attributes);
    }

    if(!problem.empty()) {
        return problem;
    }

    if(command.domain == "select" && !listsOption(device->attributes, "options", command.data.at("option"))) {
        return "Unsupported select option";
    }

    const bool isDamper =
        command.domain == "cover" && device->attributes.value("device_class", string()) == "damper";

    if(isDamper && command.data.contains("position") &&
       std::fmod(command.data["position"].get<double>(), 5) != 0) {
        return "Damper position must use 5 percent increments";
    }

    const bool changesMyZone = command.domain == "select" &&
                               std::any_of(policy.begin(), policy.end(), [&](const AirflowSystem& system) {
                                   return system.myZoneEntity == command.entity;
                               });

    if(changesMyZone) {
        return "Change MyZone on the controller; voice does not change the installation's constant-zone "
               "selection";
    }

    // Every climate and damper command needs a verified installation policy, so a zone's
    // heat_cool state is never mistaken for the compressor's mode.
    if(command.domain == "climate" || isDamper) {
        return checkAirflow(command, devices, policy);
    }

    return "";
}

/**
 * @brief Whether a policy entry has a nonempty list under a key.
 */
bool hasNonemptyList(const json& entry, const char* key) {
    return entry.contains(key) && entry[key].is_array() && !entry[key].empty();
}
} // namespace

AirflowPolicy readAirflowPolicy(const json& configuration) {
    if(!configuration.is_object()) {
        throw runtime_error("Control configuration must be an object");
    }

    if(!configuration.contains("advantage_air")) {
        return {};
    }

    const json& section = configuration["advantage_air"];

    if(!section.is_object() || !section.contains("systems") || !section["systems"].is_array()) {
        throw runtime_error("advantage_air requires a systems list");
    }

    AirflowPolicy policy;
    std::set<string> entities;

    for(const auto& entry : section["systems"]) {
        const bool complete = entry.is_object() && entry.value("entity", json()).is_string() &&
                              hasNonemptyList(entry, "zones") && hasNonemptyList(entry, "constant_zones") &&
                              entry.value("minimum_open_zones", json()).is_number_integer() &&
                              entry.value("verified_preset", json()).is_string();

        if(!complete) {
            throw runtime_error(
                "AdvantageAir systems need an entity, zones, constant_zones, and minimum_open_zones");
        }

        AirflowSystem system;
        system.entity = entry["entity"].get<string>();
        system.myZoneEntity = entry.value("myzone_entity", string());
        system.verifiedPreset = entry["verified_preset"].get<string>();
        const int minimum = entry["minimum_open_zones"].get<int>();

        if(system.entity.compare(0, 8, "climate.") != 0 || !entities.insert(system.entity).second ||
           minimum < 1 || minimum > static_cast<int>(entry["zones"].size())) {
            throw runtime_error("Invalid AdvantageAir main entity or minimum zone count");
        }

        system.minimumOpenZones = static_cast<unsigned>(minimum);

        for(const auto& zone : entry["zones"]) {
            if(!zone.is_string() || !entities.insert(zone.get<string>()).second) {
                throw runtime_error("AdvantageAir zones must be unique entity IDs");
            }

            system.zones.push_back(zone.get<string>());
        }

        for(const auto& zone : entry["constant_zones"]) {
            if(!zone.is_string() || !contains(system.zones, zone.get<string>())) {
                throw runtime_error("Every constant zone must be in zones");
            }

            system.constantZones.push_back(zone.get<string>());
        }

        policy.push_back(std::move(system));
    }

    return policy;
}

Commands::Result validateCommand(Command command, const Devices& devices, const ServiceCatalogue& services, const AirflowPolicy& policy) {
    try {
        string problem = resolveRelativeVolume(command, devices);

        if(problem.empty()) {
            problem = checkCommand(command, devices, services, policy);
        }

        return {problem.empty(), problem, std::move(command)};
    } catch(const std::exception& error) {
        return {false, string("Invalid command or device state: ") + error.what(), command};
    }
}