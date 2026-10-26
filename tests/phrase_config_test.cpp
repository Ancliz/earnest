#include "parser/parser.hpp"
#include "home_assistant/device_aliases.hpp"
#include "home_assistant/control_policy.hpp"
#include "parser/detail.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <unistd.h>

using Parser::json;
using std::string;

namespace {
int checks = 0;

void check(bool condition, const string& message) {
    ++checks;

    if(!condition) {
        throw std::runtime_error(message);
    }
}

json state(const string& entity, const string& status, const json& attributes = json::object()) {
    return {{"entity_id", entity}, {"state", status}, {"attributes", attributes}};
}
} // namespace

int main() {
    Parser::Engine engine;
    json states =
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
                     state("climate.ac_alasdair_bed", "heat_cool",
                           {{"friendly_name", "AC Alasdair Bed"}, {"supported_features", 385}}),
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
    Parser::Options options;
    populateDeviceAliases(states, options);
    check(Parser::resolveEntityId("ac", "climate", options) == "climate.ac", "friendly-name lookup API");
    auto parse = [&](const string& text, const string& endpoint, const string& entity,
                     const json& fields = json::object()) {
        const auto result = engine.parse(text, options);
        json body = fields;
        body["entity_id"] = entity;
        const json expected = {{"endpoint", endpoint}, {"body", body}};

        if(result != expected) {
            std::cerr << text << " expected " << expected << " got " << result << '\n';
        }

        check(result == expected, text);
        return result;
    };
    const auto temperature =
        parse("set the master bedroom temperature to twenty two", "/services/climate/set_temperature",
              "climate.ac_master_bed", {{"temperature", 22}});
    check(validateCommand(temperature, states, engine.configuration()).empty(), "zone temperature allowed");
    parse("set temperature in the master bedroom to twenty two degrees celsius",
          "/services/climate/set_temperature", "climate.ac_master_bed", {{"temperature", 22}});
    const auto start = parse("turn on the air conditioner", "/services/climate/turn_on", "climate.ac");
    check(validateCommand(start, states, engine.configuration()).empty(),
          "main start with open constant zone");
    parse("turn the tv and rump off", "/services/climate/turn_off", "climate.ac_tv_rump");
    parse("set the fan speed on the air conditioner to low", "/services/climate/set_fan_mode", "climate.ac",
          {{"fan_mode", "low"}});
    parse("start cooling the ac", "/services/climate/set_hvac_mode", "climate.ac", {{"hvac_mode", "cool"}});
    parse("set desk lamp brightness to forty percent", "/services/light/turn_on", "light.lamp",
          {{"brightness_pct", 40}});
    parse("set office fan speed to sixty", "/services/fan/set_percentage", "fan.office",
          {{"percentage", 60}});
    check(engine.parse("turn kitchen on", options).is_null(), "cross-domain ambiguity rejected");
    check(engine.parse("pause kitchen and turn the ac off", options).is_null(), "compound command rejected");
    check(engine.parse("turn on unknown device", options).is_null(), "unknown entity rejected");
    check(!validateCommand(engine.parse("turn on broken", options), states, engine.configuration()).empty(),
          "unavailable entity rejected");
    check(findAvailableDevices(states).size() + 1 == findAvailableDevices(states, true).size(),
          "available entity filtering");
    check(!validateCommand(engine.parse("set the ac temperature to twenty two", options), states,
                           engine.configuration())
               .empty(),
          "MyTemp main has no temperature support");
    check(!validateCommand(engine.parse("set master bedroom temperature to fifteen", options), states,
                           engine.configuration())
               .empty(),
          "temperature minimum");
    check(!validateCommand(engine.parse("set master bedroom temperature to twenty two point five", options),
                           states, engine.configuration())
               .empty(),
          "temperature step");
    check(!validateCommand(engine.parse("cool master bedroom", options), states, engine.configuration())
               .empty(),
          "zone cool mode is not a compressor mode");
    auto running = states;
    running[0]["state"] = "heat";
    const auto closeConstant = engine.parse("turn off the tv and rump", options);
    check(!validateCommand(closeConstant, running, engine.configuration()).empty(),
          "running constant zone cannot close");
    check(validateCommand(engine.parse("turn the ac off", options), running, engine.configuration()).empty(),
          "main can always turn off");
    auto closed = states;
    closed[4]["state"] = "off";
    check(!validateCommand(start, closed, engine.configuration()).empty(),
          "main cannot start with constant zone closed");
    auto changedPreset = states;
    changedPreset[0]["attributes"]["preset_mode"] = "MyZone";
    check(!validateCommand(start, changedPreset, engine.configuration()).empty(),
          "changed preset cannot reuse verified constant policy");
    auto missing = states;
    missing.erase(missing.begin() + 2);
    check(!validateCommand(start, missing, engine.configuration()).empty(),
          "missing zone state fails closed");
    check(!validateCommand(start, states, json::object()).empty(), "no installation policy blocks startup");
    check(!validateCommand({{"endpoint", "/services/select/select_option"},
                            {"body", {{"entity_id", "select.ac_myzone"}, {"option", "Inactive"}}}},
                           states, engine.configuration())
               .empty(),
          "voice cannot change MyZone");

    const string file = "/tmp/smarthome-phrase-test-" + std::to_string(getpid()) + ".yaml";
    const auto write = [&](const string& contents) { std::ofstream(file) << contents; };
    const string first = "version: 1\nrules:\n  - id: pause\n    domain: media_player\n    service: "
                         "media_pause\n    phrases: ['hold [the] {device}']\n";
    write(first);
    Parser::Engine dynamic(file);
    check(!dynamic.parse("hold kitchen", options).is_null(), "custom phrase file");
    check(!dynamic.parse("hold the kitchen", options).is_null(), "optional determiner");
    dynamic.addPhrase("pause", "freeze {device}");
    check(!dynamic.parse("freeze kitchen", options).is_null(), "addPhrase API");
    write("version: [broken");
    check(!dynamic.reloadIfChanged() && !dynamic.reloadError().empty(), "invalid YAML reload rejected");
    check(!dynamic.parse("hold kitchen", options).is_null(), "invalid reload keeps prior rules");
    write(first + "    body: {volume_level: '{missing}'}\n");
    check(!dynamic.reloadIfChanged(), "missing template slot rejected");
    write(first + "    body: {}\n");
    check(dynamic.reloadIfChanged() && dynamic.reloadError().empty(), "valid reload clears prior error");
    check(dynamic.parse("freeze kitchen", options).is_null(), "reload restores file-backed patterns");
    bool duplicateKey = false;

    try {
        readYaml("version: 1\nversion: 2\n");
    } catch(const std::exception&) {
        duplicateKey = true;
    }

    check(duplicateKey, "duplicate YAML keys rejected");
    std::filesystem::remove(file);
    check(!dynamic.reloadIfChanged() && !dynamic.reloadError().empty(),
          "deleted config keeps previous rules");
    check(!dynamic.parse("hold kitchen", options).is_null(), "deleted config does not replace active rules");
    write(first + "    body: {}\n");
    dynamic.reloadIfChanged();
    check(dynamic.reloadError().empty(), "restored unchanged file clears read error");
    std::filesystem::remove(file);
    std::cout << checks << " configurable phrase and safety checks passed\n";
}
