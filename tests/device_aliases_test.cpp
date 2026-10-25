#include "../device_aliases.hpp"

#include <iostream>
#include <stdexcept>

using std::string;
using Parser::json;
using Parser::Options;

int checks = 0;

void check(bool valid, const char* message) {
    ++checks;

    if(!valid) {
        throw std::runtime_error(message);
    }
}

int main() {
    Options options;
    options.defaultDevice = "media_player.default";
    options.conversationAgent = "conversation.home_assistant";
    options.fuzzyDevices = false;
    const json states = {
        {{"entity_id", "media_player.rumpus_room_q990b"}, {"attributes", {{"friendly_name", "Q990"}}}},
        {{"entity_id", "light.q990"}, {"attributes", {{"friendly_name", "Q990"}}}},
        {{"entity_id", "media_player.office"}, {"attributes", json::object()}},
        {{"entity_id", "media_player.bedroom"}, {"attributes", nullptr}},
        {{"entity_id", "media_player.spare"}, {"attributes", {{"friendly_name", 42}}}},
        {{"entity_id", "media_player."}}, {{"entity_id", 42}}, nullptr
    };
    populateDeviceAliases(states, options);
    check(options.devices.at("q990") == "media_player.rumpus_room_q990b", "friendly name alias");
    check(options.devices.at("rumpus room q990b") == "media_player.rumpus_room_q990b", "readable entity name");
    check(options.devices.at("media_player.rumpus_room_q990b") == "media_player.rumpus_room_q990b", "full ID alias");
    check(options.devices.at("office") == "media_player.office", "missing friendly name fallback");
    check(options.devices.at("bedroom") == "media_player.bedroom", "null attributes fallback");
    check(options.devices.at("spare") == "media_player.spare", "invalid friendly name ignored");
    check(options.defaultDevice == "media_player.default" && !options.fuzzyDevices &&
        options.conversationAgent == "conversation.home_assistant", "preserve other options");
    const auto request = Parser::parse("set the volume on the Q990 to 0.2.", options);
    check(request == json({{"endpoint", "/services/media_player/volume_set"},
        {"body", {{"entity_id", "media_player.rumpus_room_q990b"}, {"volume_level", 0.2}}}}), "reported command resolves entity");
    check(Parser::parse("pause unknown", options).is_null(), "unknown speaker rejected");
    auto duplicates = json::array({
        {{"entity_id", "media_player.first"}, {"attributes", {{"friendly_name", " The Kitchen "}}}},
        {{"entity_id", "media_player.second"}, {"attributes", {{"friendly_name", "kitchen"}}}},
        {{"entity_id", "media_player.third"}, {"attributes", {{"friendly_name", "KITCHEN's"}}}}
    });
    populateDeviceAliases(duplicates, options);
    check(options.devices.at("kitchen").empty(), "duplicate names blocked");
    check(Parser::parse("pause kitchen", options).is_null(), "ambiguous exact alias rejected");
    options.fuzzyDevices = true;
    check(Parser::parse("pause kichen", options).is_null(), "ambiguous fuzzy alias rejected");
    check(Parser::parse("pause first", options)["body"]["entity_id"] == "media_player.first", "unique alias remains usable");
    check(options.devices.count("q990") == 0, "refresh removes stale aliases");
    duplicates.push_back(duplicates[0]);
    populateDeviceAliases(duplicates, options);
    check(options.devices.at("first") == "media_player.first", "same entity repeat is harmless");
    const auto before = options.devices;

    for(const auto& invalid : {json::object(), json::array(), json::array({{{"entity_id", "light.kitchen"}}})}) {
        bool rejected = false;

        try {
            populateDeviceAliases(invalid, options);
        } catch(const std::runtime_error&) {
            rejected = true;
        }

        check(rejected && options.devices == before, "invalid data leaves previous aliases intact");
    }

    std::cout << checks << " device alias checks passed\n";
}
