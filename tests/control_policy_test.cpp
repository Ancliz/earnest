/**
 * @file
 * @brief Regression suite for discovery, command validation, and the AdvantageAir airflow policy.
 */

#include "check.hpp"
#include "fixtures.hpp"
#include "config/configuration.hpp"
#include "home_assistant/home_assistant.hpp"
#include "parser/regex_parser.hpp"

#include <cmath>
#include <iostream>
#include <vector>

using Commands::Command;
using Commands::Context;
using Commands::findService;
using Commands::json;
using std::string;

namespace {
/**
 * @brief One synthetic Home Assistant state record.
 */
json state(const string& entity, const string& status, const json& attributes = json::object()) {
    return {{"entity_id", entity}, {"state", status}, {"attributes", attributes}};
}
} // namespace

/**
 * @brief Verify configured parsing against multi-domain devices and every independent control guard.
 */
int main() {
    const auto configuration = readTestConfiguration();
    const auto policy = readAirflowPolicy(configuration);
    const auto services = readServiceCatalogue(readFixture("services.json"), readVoiceScope(configuration));
    RegexParser parser(configuration);
    const json states =
        json::array({state("climate.ac", "off",
                           {{"friendly_name", "AC"},
                            {"hvac_modes", {"off", "cool", "heat", "fan_only", "dry"}},
                            {"supported_features", 408},
                            {"fan_modes", {"low", "medium", "high", "auto"}},
                            {"preset_mode", "MyTemp"}}),
                     state("climate.ac_master_bed", "heat_cool",
                           {{"friendly_name", "AC Master Bed"},
                            {"supported_features", 385},
                            {"min_temp", 16},
                            {"max_temp", 32},
                            {"target_temp_step", 1},
                            {"hvac_modes", {"off", "heat_cool"}}}),
                     state("climate.ac_guest_bed", "heat_cool",
                           {{"friendly_name", "AC Guest Bed"}, {"supported_features", 385}}),
                     state("climate.ac_craft_ldy", "off",
                           {{"friendly_name", "AC Craft Ldy"}, {"supported_features", 385}}),
                     state("climate.ac_tv_rump", "heat_cool",
                           {{"friendly_name", "AC TV & Rump"}, {"supported_features", 385}}),
                     state("climate.ac_diningliving", "off",
                           {{"friendly_name", "AC DiningLiving"}, {"supported_features", 385}}),
                     state("switch.ac_myfan", "on", {{"friendly_name", "AC MyFan"}}),
                     state("select.ac_myzone", "Master Bed",
                           {{"friendly_name", "AC MyZone"}, {"options", {"Inactive", "Master Bed"}}}),
                     state("media_player.kitchen", "playing", {{"friendly_name", "Kitchen"}}),
                     state("light.kitchen", "off", {{"friendly_name", "Kitchen"}}),
                     state("light.lamp", "off", {{"friendly_name", "Desk Lamp"}}),
                     state("fan.office", "on", {{"friendly_name", "Office Fan"}}),
                     state("switch.broken", "unavailable", {{"friendly_name", "Broken"}})});
    const auto devices = controllableDevices(states, services);
    const Context context{devices};
    /*
     * @brief Parse text, require a command, and check its exact form.
     */
    const auto expect = [&](const string& text, const Command& expected) {
        const auto command = parser.parse(text, context);

        if(command != expected) {
            std::cerr << text << " expected " << json(expected) << " got "
                      << (command ? json(*command) : json()) << '\n';
        }

        check(command == expected, text);
        return expected;
    };
    /*
     * @brief Parse text, require a command, and return why it is rejected against a state snapshot.
     */
    const auto rejection = [&](const string& text, const json& snapshot) {
        const auto command = parser.parse(text, context);
        check(command.has_value(), "parses: " + text);
        return validateCommand(*command, controllableDevices(snapshot, services), services, policy).error;
    };

    const auto temperature =
        expect("set the master bedroom temperature to twenty two",
               {"climate", "set_temperature", "climate.ac_master_bed", {{"temperature", 22}}});
    check(validateCommand(temperature, devices, services, policy).success, "zone temperature allowed");
    expect("set temperature in the master bedroom to twenty two degrees celsius",
           {"climate", "set_temperature", "climate.ac_master_bed", {{"temperature", 22}}});
    const auto start =
        expect("turn on the air conditioner", {"climate", "turn_on", "climate.ac", json::object()});
    check(validateCommand(start, devices, services, policy).success, "main unit starts with its constant zone open");
    expect("turn the tv and rump off", {"climate", "turn_off", "climate.ac_tv_rump", json::object()});
    expect("set the fan speed on the air conditioner to low",
           {"climate", "set_fan_mode", "climate.ac", {{"fan_mode", "low"}}});
    expect("start cooling the ac", {"climate", "set_hvac_mode", "climate.ac", {{"hvac_mode", "cool"}}});
    expect("set desk lamp brightness to forty percent",
           {"light", "turn_on", "light.lamp", {{"brightness_pct", 40}}});
    expect("set office fan speed to sixty", {"fan", "set_percentage", "fan.office", {{"percentage", 60}}});
    check(!parser.parse("turn kitchen on", context), "cross-domain ambiguity rejected");
    check(!parser.parse("pause kitchen and turn the ac off", context), "compound command rejected");
    check(!parser.parse("turn on unknown device", context), "unknown entity rejected");

    check(!rejection("turn on broken", states).empty(), "unavailable entity rejected");
    check(!rejection("set the ac temperature to twenty two", states).empty(), "MyTemp main has no setpoint");
    check(!rejection("set master bedroom temperature to fifteen", states).empty(), "temperature minimum");
    check(!rejection("set master bedroom temperature to twenty two point five", states).empty(),
          "temperature step");
    check(!rejection("cool master bedroom", states).empty(), "zone cool mode is not a compressor mode");
    auto running = states;
    running[0]["state"] = "heat";
    check(!rejection("turn off the tv and rump", running).empty(), "running constant zone cannot close");
    check(rejection("turn the ac off", running).empty(), "main unit can always turn off");
    auto closed = states;
    closed[4]["state"] = "off";
    check(!validateCommand(start, controllableDevices(closed, services), services, policy).success,
          "main unit cannot start with its constant zone closed");
    auto changedPreset = states;
    changedPreset[0]["attributes"]["preset_mode"] = "MyZone";
    check(!validateCommand(start, controllableDevices(changedPreset, services), services, policy).success,
          "a changed preset cannot reuse the verified constant-zone policy");
    auto missing = states;
    missing.erase(missing.begin() + 2);
    check(!validateCommand(start, controllableDevices(missing, services), services, policy).success,
          "a missing zone fails closed");
    check(validateCommand(start, devices, services, {}).success, "climate outside a configured zoned system needs no airflow policy");
    check(!validateCommand({"select", "select_option", "select.ac_myzone", {{"option", "Inactive"}}}, devices, services, policy).success,
          "voice cannot change MyZone");

    // Commands that never came from the regex parser must be rejected on their own merits.
    for(const auto& command : vector<Command>{
            {"fan", "set_percentage", "fan.office", json::object()},
            {"fan", "media_pause", "fan.office", json::object()},
            {"media_player", "set_temperature", "media_player.kitchen", {{"temperature", 22}}},
            {"media_player", "turn_on", "media_player.kitchen", {{"brightness_pct", 50}}},
            {"conversation", "process", "", {{"text", "turn the AC off"}}},
            {"light", "turn_on", "media_player.kitchen", json::object()},
            {"media_player", "volume_set", "media_player.kitchen", nullptr},
            {"climate", "set_temperature", "climate.ac_master_bed", {{"temperature", "twenty"}}},
            {"climate",
             "set_temperature",
             "climate.ac_master_bed",
             {{"temperature", 22}, {"entity_id", "climate.ac"}}},
            {"media_player", "volume_up", "media_player.kitchen", {{"amount", 2}}}}) {
        check(!validateCommand(command, devices, services, policy).success,
              "untrusted command rejected: " + json(command).dump());
    }

    auto malformedStates = states;
    malformedStates[1]["attributes"] = nullptr;
    check(!validateCommand(temperature, controllableDevices(malformedStates, services), services, policy).success,
          "malformed state is a rejection rather than an exception");
    checkThrows([] { readAirflowPolicy(json::array()); }, "a non-object configuration is rejected");
    checkThrows([] { readAirflowPolicy({{"advantage_air", {{"systems", {{{"entity", "climate.ac"}}}}}}}); },
                "an incomplete AdvantageAir system is rejected");

    const json discovery = {{{"entity_id", "media_player.office"}, {"attributes", json::object()}},
                            {{"entity_id", "media_player.bedroom"}, {"attributes", nullptr}},
                            {{"entity_id", "media_player.spare"}, {"attributes", {{"friendly_name", 42}}}},
                            {{"entity_id", "sensor.kitchen"}},
                            {{"entity_id", "media_player."}},
                            {{"entity_id", 42}},
                            nullptr};
    const auto discovered = controllableDevices(discovery, services);
    check(discovered.size() == 3 && discovered[0].name == "office" && discovered[1].name == "bedroom" &&
              discovered[1].attributes.is_object() && discovered[2].name == "spare",
          "discovery skips malformed entities and falls back to object IDs");
    checkThrows([&] { controllableDevices(json::object(), services); }, "discovery requires a state array");

    // The catalogue comes from Home Assistant's own service list, within the voice scope.
    const auto* volumeSet = findService(services, "media_player", "volume_set");
    check(volumeSet && volumeSet->required.count("volume_level") && volumeSet->selectors.at("volume_level")["number"]["max"] == 1,
          "fields, required flags and selectors are discovered");
    check(findService(services, "climate", "set_temperature")->allowed.count("target_temp_high"), "fields inside collapsible sections are discovered");
    check(findService(services, "media_player", "volume_up")->allowed.count("amount"), "relative volume accepts an amount");
    const auto speaker = controllableDevices(json::array({state("media_player.kitchen", "playing", {{"volume_level", 0.5}})}), services);
    const auto stepped = validateCommand({"media_player", "volume_down", "media_player.kitchen", json::object()}, speaker, services, policy);
    check(stepped.success && stepped.command.service == "volume_set" && std::abs(stepped.command.data["volume_level"].get<double>() - 0.48) < 1e-9,
          "relative volume without an amount moves by the default step");
    check(!services.count("lock") && !services.count("homeassistant") && !services.count("conversation"), "security and system domains are excluded by default");
    check(!validateCommand({"light", "turn_on", "light.lamp", {{"brightness_pct", 140}}}, devices, services, policy).success, "number selectors bound values");
    check(!validateCommand({"media_player", "volume_mute", "media_player.kitchen", {{"is_volume_muted", "yes"}}}, devices, services, policy).success,
          "boolean selectors require booleans");
    check(controllableDevices(json::array({state("lock.front_door", "locked")}), services).empty(), "devices in excluded domains are not discovered");

    const json scoped = {{"voice_control", {{"allow", {"lock"}}, {"exclude", {"media_player.volume_mute", "fan"}}}}};
    const auto adjusted = readServiceCatalogue(readFixture("services.json"), readVoiceScope(scoped));
    check(adjusted.count("lock") && !adjusted.count("fan"), "voice_control allows and excludes domains");
    check(!findService(adjusted, "media_player", "volume_mute") && findService(adjusted, "media_player", "volume_set"), "voice_control excludes single services");
    checkThrows([] { readVoiceScope({{"voice_control", {{"exclude", "lock"}}}}); }, "voice_control lists must be lists");
    checkThrows([] { readServiceCatalogue(json::object(), {}); }, "services must be an array");
    std::cout << checks << " control policy checks passed\n";
}