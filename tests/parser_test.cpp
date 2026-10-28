/**
 * @file
 * @brief Regression suite for parser behavior.
 */

#include "parser/parser.hpp"
#include "config/configuration.hpp"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>

using Parser::json;
using Parser::Options;
using std::string;

int checks = 0;

/**
 * @brief Count a parser assertion and stop the test process on failure.
 *
 * @param[in] condition Whether the expected parser behavior was observed.
 * @param[in] input Input phrase or assertion label included in a failure diagnostic.
 * @note Increments the suite counter; a false condition writes stderr and exits with
 *       status one rather than throwing or continuing the remaining checks.
 */
void check(bool condition, const string& input) {
    ++checks;

    if(!condition) {
        std::cerr << "FAIL: " << input << '\n';
        std::exit(1);
    }
}

/**
 * @brief Assert that one spoken command produces the expected request representation.
 *
 * Creates a non-strict media context with a default target and builds either a
 * service request or a signed relative-volume intent. On mismatch, prints both
 * representations before handing failure to check().
 *
 * @param[in] parser Parser interface under test; the helper does not select a backend.
 * @param[in] input Spoken command to interpret.
 * @param[in] action Expected media service name, also used to sign relative deltas.
 * @param[in] target Expected target string, or null to use the fixture's default target.
 * @param[in] parameters Expected service fields, or an amount for relative volume.
 * @note Runs parsing/assertions only; no request is sent to a device.
 */
void command(const Commands::Parser& parser, const string& input, const string& action, const json& target,
             const json& parameters = json::object()) {
    Options options;
    const string entity = target.is_string() ? target.get<string>() : "media_player.default";
    options.defaultDevice = "media_player.default";
    const auto actual = parser.parse(input, options);
    json body = parameters;
    body["entity_id"] = entity;
    string service = action;

    json expected;

    if(body.contains("amount")) {
        body["amount"] = body["amount"].get<double>() * (action == "volume_down" ? -1 : 1);
        expected = {{"endpoint", "/commands/media_player/adjust_volume"}, {"body", body}};
    } else {
        expected = {{"endpoint", "/services/media_player/" + service}, {"body", body}};
    }

    if(actual != expected) {
        std::cerr << "Expected " << expected << ", got " << actual << '\n';
    }

    check(actual == expected, input);
}

/**
 * @brief Run grammar, spoken-number, target-resolution, and ambiguity regressions.
 *
 * Loads the project's default phrase YAML into an explicit regex parser, then
 * checks supported speaker variants, invalid values, fuzzy aliases, unresolved
 * media targets, relative-volume intents, and default-domain restrictions.
 *
 * @return Zero when all assertions complete; check() exits with one on failure.
 * @note Reads configuration and writes test diagnostics; never contacts Home Assistant.
 */
int main() {
    Parser::RegexParser parser(Config::readYamlFile(Config::defaultFile()));
    for(const auto& input :
        {"set the volume of the living room speaker to forty", "set the living room speaker volume to 40",
         "set the living room speaker's volume to forty percent",
         "change volume on living room speaker to 40%",
         "adjust the volume for the living room speaker at forty",
         "put volume in living room speaker to forty", "volume of living room speaker 40",
         "living room speaker volume 40", "set volume to forty on the living room speaker",
         "set the volume to forty for living room speaker", "turn living room speaker to 0.4",
         "Could you please set the volume of the living room speaker to forty, please?",
         "set the volumn of living room speaker to forty"}) {
        command(parser, input, "volume_set", "living room speaker", {{"volume_level", 0.4}});
    }

    command(parser, "turn the kitchen volume up by ten", "volume_up", "kitchen", {{"amount", .1}});
    command(parser, "turn the kitchen volume down", "volume_down", "kitchen");
    command(parser, "volume zero point four", "volume_set", nullptr, {{"volume_level", .4}});
    command(parser, "volume point five", "volume_set", nullptr, {{"volume_level", .5}});
    command(parser, "volume one point zero", "volume_set", nullptr, {{"volume_level", 1.0}});
    command(parser, "volume twenty-one", "volume_set", nullptr, {{"volume_level", .21}});
    command(parser, "set volume to 1", "volume_set", nullptr, {{"volume_level", .01}});
    command(parser, "set volume to 1.0", "volume_set", nullptr, {{"volume_level", 1.0}});
    command(parser, "set volume to one hundred percent", "volume_set", nullptr, {{"volume_level", 1.0}});
    command(parser, "set kitchen to full volume", "volume_set", "kitchen", {{"volume_level", 1.0}});
    command(parser, "set volume to half", "volume_set", nullptr, {{"volume_level", .5}});
    command(parser, "set volume to zero", "volume_set", nullptr, {{"volume_level", 0}});

    for(const auto& input :
        {"turn kitchen up by ten", "raise kitchen volume by ten percent", "increase volume on kitchen by ten",
         "turn volume up on kitchen by ten", "volume up in kitchen by ten", "make kitchen louder by ten"}) {
        command(parser, input, "volume_up", "kitchen", {{"amount", .1}});
    }

    for(const auto& input : {"turn kitchen down by ten", "lower kitchen volume by ten percent",
                             "decrease volume on kitchen by ten", "turn volume down on kitchen by ten",
                             "volume down in kitchen by ten", "make kitchen quieter by ten"}) {
        command(parser, input, "volume_down", "kitchen", {{"amount", .1}});
    }

    for(const auto& input : {"turn the volume up", "turn up", "volume up", "raise volume"}) {
        command(parser, input, "volume_up", nullptr);
    }

    command(parser, "turn volume down by five", "volume_down", nullptr, {{"amount", .05}});
    command(parser, "turn up by ten", "volume_up", nullptr, {{"amount", .1}});
    command(parser, "turn kitchen down", "volume_down", "kitchen");
    command(parser, "mute the kitchen", "volume_mute", "kitchen", {{"is_volume_muted", true}});
    command(parser, "unmute the sound on the kitchen", "volume_mute", "kitchen",
            {{"is_volume_muted", false}});
    command(parser, "kitchen mute", "volume_mute", "kitchen", {{"is_volume_muted", true}});
    command(parser, "mute", "volume_mute", nullptr, {{"is_volume_muted", true}});
    command(parser, "play the music on the kitchen", "media_play", "kitchen");
    command(parser, "resume kitchen", "media_play", "kitchen");
    command(parser, "pause", "media_pause", nullptr);
    command(parser, "pause music in kitchen", "media_pause", "kitchen");
    command(parser, "kitchen pause", "media_pause", "kitchen");
    command(parser, "stop playback on kitchen", "media_stop", "kitchen");
    command(parser, "toggle music on kitchen", "media_play_pause", "kitchen");
    command(parser, "skip to the next track on kitchen", "media_next_track", "kitchen");
    command(parser, "skip this song", "media_next_track", nullptr);
    command(parser, "next song on kitchen", "media_next_track", "kitchen");
    command(parser, "go back one song in kitchen", "media_previous_track", "kitchen");
    command(parser, "turn on the kitchen", "turn_on", "kitchen");
    command(parser, "switch the kitchen off", "turn_off", "kitchen");

    for(const auto& input :
        {"", "hello", "set volume to 101", "set volume to -10", "set volume to - 10",
         "set volume to minus ten", "set volume to twenty fifty", "set volume to ten cats",
         "volume one point five", "volume zero point twenty", "don't pause kitchen", "do not mute kitchen",
         "pause kitchen and play bedroom", "set volume to 99999999999999999999999999999999"}) {
        check(parser.parse(input).is_null(), input);
    }

    check(parser.parse(string(513, 'x')).is_null(), "length limit");
    Options options;
    options.devices = {{"kitchen", "media_player.kitchen"}, {"living room speaker", "media_player.lounge"}};
    check(parser.parse("pause the kitchen", options)["body"]["entity_id"] == "media_player.kitchen",
          "exact alias");
    check(parser.parse("pause kichen", options)["body"]["entity_id"] == "media_player.kitchen",
          "fuzzy alias");
    check(parser.parse("pause living rom speker", options)["body"]["entity_id"] == "media_player.lounge",
          "two edits");
    check(parser.parse("pause unknown", options).is_null(), "unknown device");
    options.fuzzyDevices = false;
    check(parser.parse("pause kichen", options).is_null(), "fuzzy disabled");
    options.fuzzyDevices = true;
    options.devices = {{"kitchan", "a"}, {"kitchon", "b"}};
    check(parser.parse("pause kitchen", options).is_null(), "ambiguous aliases");
    options.devices = {{"kitchan", "a"}, {"kitchon", "a"}};
    check(parser.parse("pause kitchen", options)["body"]["entity_id"] == "a", "same entity aliases");
    options.devices = {{"kitchen", "media_player.kitchen"}};
    options.defaultDevice = "media_player.kitchen";
    auto request = parser.parse("pause", options);
    check(request == json({{"endpoint", "/services/media_player/media_pause"},
                           {"body", {{"entity_id", "media_player.kitchen"}}}}),
          "default device");
    request = parser.parse("pause");
    check(request["body"].contains("entity_id") && request["body"]["entity_id"].is_null(),
          "unresolved target");
    request = parser.parse("turn up by ten");
    check(request == json({{"endpoint", "/commands/media_player/adjust_volume"},
                           {"body", {{"entity_id", nullptr}, {"amount", 0.1}}}}),
          "relative command retains unresolved target for executor rejection");
    request = parser.parse("Please turn kitchen up by ten percent!", options);
    check(request["body"] == json({{"entity_id", "media_player.kitchen"}, {"amount", 0.1}}),
          "relative volume is a structured intent, not arbitrary conversation text");
    request = parser.parse("pause kitchen", options);
    check(request["body"] == json({{"entity_id", "media_player.kitchen"}}),
          "service body has no conversation fields");
    check(parser.parse("turn kitchen up by 101 percent", options).is_null(), "invalid relative amount");
    options.defaultDevice = "climate.ac";
    check(parser.parse("pause", options)["body"]["entity_id"].is_null(),
          "a default device from another domain is never used as a media target");
    std::cout << checks << " parser checks passed\n";
}
