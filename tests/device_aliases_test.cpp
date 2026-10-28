/**
 * @file
 * @brief Regression suite for device aliases behavior.
 */

#include "parser/parser.hpp"
#include "config/configuration.hpp"
#include "home_assistant/device_aliases.hpp"

#include <iostream>
#include <stdexcept>

using Parser::json;
using Parser::Options;
using std::string;

int checks = 0;

/**
 * @brief Count a discovery assertion and raise a descriptive failure.
 *
 * @param[in] valid Whether the fixture produced the expected context or parse result.
 * @param[in] message Description copied into the exception on failure.
 * @throws std::runtime_error If valid is false.
 * @note Increments the suite's assertion counter.
 */
void check(bool valid, const char* message) {
    ++checks;

    if(!valid) {
        throw std::runtime_error(message);
    }
}

/**
 * @brief Verify discovery aliases, collision handling, refresh replacement, and input isolation.
 *
 * Uses synthetic entity snapshots to check name fallbacks, domain scoping, fuzzy
 * ambiguity, repeated entities, preserved user preferences, and rejection of empty
 * or invalid discovery data without mutating the caller's prior options.
 *
 * @return Zero when all checks complete.
 * @throws std::runtime_error If an assertion or fixture initialization fails.
 * @note Reads default phrase configuration, but makes no HTTP or device-control calls.
 */
int main() {
    Parser::RegexParser parser(Config::readYamlFile(Config::defaultFile()));
    Options options;
    options.defaultDevice = "media_player.default";
    options.fuzzyDevices = false;
    const json states = {
        {{"entity_id", "media_player.rumpus_room_q990b"}, {"attributes", {{"friendly_name", "Q990"}}}},
        {{"entity_id", "light.q990"}, {"attributes", {{"friendly_name", "Q990"}}}},
        {{"entity_id", "media_player.office"}, {"attributes", json::object()}},
        {{"entity_id", "media_player.bedroom"}, {"attributes", nullptr}},
        {{"entity_id", "media_player.spare"}, {"attributes", {{"friendly_name", 42}}}},
        {{"entity_id", "media_player."}},
        {{"entity_id", 42}},
        nullptr};
    options = buildCommandContext(states, options);
    check(options.devices.at("q990") == "media_player.rumpus_room_q990b", "friendly name alias");
    check(options.devices.at("rumpus room q990b") == "media_player.rumpus_room_q990b",
          "readable entity name");
    check(options.devices.at("media_player.rumpus_room_q990b") == "media_player.rumpus_room_q990b",
          "full ID alias");
    check(options.devices.at("office") == "media_player.office", "missing friendly name fallback");
    check(options.devices.at("bedroom") == "media_player.bedroom", "null attributes fallback");
    check(options.devices.at("spare") == "media_player.spare", "invalid friendly name ignored");
    check(options.defaultDevice == "media_player.default" && !options.fuzzyDevices, "preserve other options");
    const auto request = parser.parse("set the volume on the Q990 to 0.2.", options);
    check(request ==
              json({{"endpoint", "/services/media_player/volume_set"},
                    {"body", {{"entity_id", "media_player.rumpus_room_q990b"}, {"volume_level", 0.2}}}}),
          "reported command resolves entity");
    check(parser.parse("pause unknown", options).is_null(), "unknown speaker rejected");
    auto duplicates = json::array(
        {{{"entity_id", "media_player.first"}, {"attributes", {{"friendly_name", " The Kitchen "}}}},
         {{"entity_id", "media_player.second"}, {"attributes", {{"friendly_name", "kitchen"}}}},
         {{"entity_id", "media_player.third"}, {"attributes", {{"friendly_name", "KITCHEN's"}}}}});
    options = buildCommandContext(duplicates, options);
    check(options.devices.at("kitchen").empty(), "duplicate names blocked");
    check(parser.parse("pause kitchen", options).is_null(), "ambiguous exact alias rejected");
    options.fuzzyDevices = true;
    check(parser.parse("pause kichen", options).is_null(), "ambiguous fuzzy alias rejected");
    check(parser.parse("pause first", options)["body"]["entity_id"] == "media_player.first",
          "unique alias remains usable");
    check(options.devices.count("q990") == 0, "refresh removes stale aliases");
    duplicates.push_back(duplicates[0]);
    options = buildCommandContext(duplicates, options);
    check(options.devices.at("first") == "media_player.first", "same entity repeat is harmless");
    const auto before = options.devices;

    for(const auto& invalid :
        {json::object(), json::array(), json::array({{{"entity_id", "sensor.kitchen"}}})}) {
        bool rejected = false;

        try {
            options = buildCommandContext(invalid, options);
        } catch(const std::runtime_error&) {
            rejected = true;
        }

        check(rejected && options.devices == before, "invalid data leaves previous aliases intact");
    }

    std::cout << checks << " device alias checks passed\n";
}
