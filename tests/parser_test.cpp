#include "../parser.hpp"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>

using std::string;
using Parser::json;
using Parser::Options;

int checks = 0;
void check(bool condition, const string& input) {
    ++checks;
    if (!condition) {
        std::cerr << "FAIL: " << input << '\n';
        std::exit(1);
    }
}
void command(const string& input, const string& action, const json& target,
             const json& parameters = json::object()) {
    Options options;
    const string entity = target.is_string() ? target.get<string>() : "media_player.default";
    options.defaultDevice = "media_player.default";
    const auto actual = Parser::parse(input, options);
    json body = parameters;
    body["entity_id"] = entity;
    string service = action;

    json expected;

    if(body.contains("amount")) {
        expected = {{"endpoint", "/conversation/process"}, {"body", {{"text", input}, {"language", "en"}}}};
    } else {
        expected = {{"endpoint", "/services/media_player/" + service}, {"body", body}};
    }

    if(actual != expected) {
        std::cerr << "Expected " << expected << ", got " << actual << '\n';
    }

    check(actual == expected, input);
}
int main() {
    for (const auto& input : {
        "set the volume of the living room speaker to forty",
        "set the living room speaker volume to 40",
        "set the living room speaker's volume to forty percent",
        "change volume on living room speaker to 40%",
        "adjust the volume for the living room speaker at forty",
        "put volume in living room speaker to forty",
        "volume of living room speaker 40",
        "living room speaker volume 40",
        "set volume to forty on the living room speaker",
        "set the volume to forty for living room speaker",
        "turn living room speaker to 0.4",
        "Could you please set the volume of the living room speaker to forty, please?",
        "set the volumn of living room speaker to forty"
    }) command(input, "volume_set", "living room speaker", {{"volume_level", 0.4}});
    command("turn the kitchen volume up by ten", "volume_up", "kitchen", {{"amount", .1}});
    command("turn the kitchen volume down", "volume_down", "kitchen");
    command("volume zero point four", "volume_set", nullptr, {{"volume_level", .4}});
    command("volume point five", "volume_set", nullptr, {{"volume_level", .5}});
    command("volume one point zero", "volume_set", nullptr, {{"volume_level", 1.0}});
    command("volume twenty-one", "volume_set", nullptr, {{"volume_level", .21}});
    command("set volume to 1", "volume_set", nullptr, {{"volume_level", .01}});
    command("set volume to 1.0", "volume_set", nullptr, {{"volume_level", 1.0}});
    command("set volume to one hundred percent", "volume_set", nullptr, {{"volume_level", 1.0}});
    command("set kitchen to full volume", "volume_set", "kitchen", {{"volume_level", 1.0}});
    command("set volume to half", "volume_set", nullptr, {{"volume_level", .5}});
    command("set volume to zero", "volume_set", nullptr, {{"volume_level", 0}});
    for (const auto& input : {"turn kitchen up by ten", "raise kitchen volume by ten percent",
        "increase volume on kitchen by ten", "turn volume up on kitchen by ten",
        "volume up in kitchen by ten", "make kitchen louder by ten"})
        command(input, "volume_up", "kitchen", {{"amount", .1}});
    for (const auto& input : {"turn kitchen down by ten", "lower kitchen volume by ten percent",
        "decrease volume on kitchen by ten", "turn volume down on kitchen by ten",
        "volume down in kitchen by ten", "make kitchen quieter by ten"})
        command(input, "volume_down", "kitchen", {{"amount", .1}});
    for (const auto& input : {"turn the volume up", "turn up", "volume up", "raise volume"})
        command(input, "volume_up", nullptr);
    command("turn volume down by five", "volume_down", nullptr, {{"amount", .05}});
    command("turn up by ten", "volume_up", nullptr, {{"amount", .1}});
    command("turn kitchen down", "volume_down", "kitchen");
    command("mute the kitchen", "volume_mute", "kitchen", {{"is_volume_muted", true}});
    command("unmute the sound on the kitchen", "volume_mute", "kitchen", {{"is_volume_muted", false}});
    command("kitchen mute", "volume_mute", "kitchen", {{"is_volume_muted", true}});
    command("mute", "volume_mute", nullptr, {{"is_volume_muted", true}});
    command("play the music on the kitchen", "media_play", "kitchen");
    command("resume kitchen", "media_play", "kitchen");
    command("pause", "media_pause", nullptr);
    command("pause music in kitchen", "media_pause", "kitchen");
    command("kitchen pause", "media_pause", "kitchen");
    command("stop playback on kitchen", "media_stop", "kitchen");
    command("toggle music on kitchen", "media_play_pause", "kitchen");
    command("skip to the next track on kitchen", "media_next_track", "kitchen");
    command("skip this song", "media_next_track", nullptr);
    command("next song on kitchen", "media_next_track", "kitchen");
    command("go back one song in kitchen", "media_previous_track", "kitchen");
    command("turn on the kitchen", "turn_on", "kitchen");
    command("switch the kitchen off", "turn_off", "kitchen");
    for (const auto& input : {"", "hello", "set volume to 101", "set volume to -10",
        "set volume to - 10", "set volume to minus ten", "set volume to twenty fifty",
        "set volume to ten cats", "volume one point five", "volume zero point twenty", "don't pause kitchen", "do not mute kitchen",
        "pause kitchen and play bedroom", "set volume to 99999999999999999999999999999999"})
        check(Parser::parse(input).is_null(), input);
    check(Parser::parse(string(513, 'x')).is_null(), "length limit");
    Options options;
    options.devices = {{"kitchen", "media_player.kitchen"}, {"living room speaker", "media_player.lounge"}};
    check(Parser::parse("pause the kitchen", options)["body"]["entity_id"] == "media_player.kitchen", "exact alias");
    check(Parser::parse("pause kichen", options)["body"]["entity_id"] == "media_player.kitchen", "fuzzy alias");
    check(Parser::parse("pause living rom speker", options)["body"]["entity_id"] == "media_player.lounge", "two edits");
    check(Parser::parse("pause unknown", options).is_null(), "unknown device");
    options.fuzzyDevices = false;
    check(Parser::parse("pause kichen", options).is_null(), "fuzzy disabled");
    options.fuzzyDevices = true;
    options.devices = {{"kitchan", "a"}, {"kitchon", "b"}};
    check(Parser::parse("pause kitchen", options).is_null(), "ambiguous aliases");
    options.devices = {{"kitchan", "a"}, {"kitchon", "a"}};
    check(Parser::parse("pause kitchen", options)["body"]["entity_id"] == "a", "same entity aliases");
    options.devices = {{"kitchen", "media_player.kitchen"}};
    options.defaultDevice = "media_player.kitchen";
    auto request = Parser::parse("pause", options);
    check(request == json({{"endpoint", "/services/media_player/media_pause"},
        {"body", {{"entity_id", "media_player.kitchen"}}}}), "default device");
    request = Parser::parse("pause");
    check(request["body"].contains("entity_id") && request["body"]["entity_id"].is_null(), "unresolved target");
    request = Parser::parse("turn up by ten");
    check(request == json({{"endpoint", "/conversation/process"},
        {"body", {{"text", "turn up by ten"}, {"language", "en"}}}}), "relative without target delegates to conversation");
    options.conversationAgent = "conversation.home_assistant";
    request = Parser::parse("Please turn kitchen up by ten percent!", options);
    check(request["body"] == json({{"text", "Please turn kitchen up by ten percent!"},
        {"language", "en"}, {"agent_id", "conversation.home_assistant"}}), "original text and agent selection");
    check(!request["body"].contains("entity_id") && !request["body"].contains("amount"), "conversation body has no service fields");
    request = Parser::parse("pause kitchen", options);
    check(request["body"] == json({{"entity_id", "media_player.kitchen"}}), "service body has no conversation fields");
    check(Parser::parse("turn kitchen up by 101 percent", options).is_null(), "invalid relative amount");
    std::cout << checks << " parser checks passed\n";
}
