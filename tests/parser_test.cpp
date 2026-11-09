/**
 * @file
 * @brief Regression suite for the regex grammar, spoken numbers, and device-name resolution.
 */

#include "check.hpp"
#include "fixtures.hpp"
#include "parser/fallback_parser.hpp"
#include "config/configuration.hpp"
#include "parser/device_names.hpp"
#include "parser/regex_parser.hpp"
#include "parser/speech_text.hpp"
#include "spotify/spotify.hpp"

#include <iostream>

using Commands::Command;
using Commands::Context;
using Commands::Device;
using Commands::json;
using Grammar::isNonSpeech;
using Grammar::stripWakeWord;
using std::string;

namespace {
/**
 * @brief A device with the given entity ID and friendly name.
 */
Device device(const string& entity, const string& name) {
    return {entity, name, entity.substr(0, entity.find('.')), "on", json::object()};
}

/**
 * @brief Speakers used by the grammar checks, with media_player.default as the default target.
 */
Context speakers() {
    Context context;
    context.devices = {device("media_player.kitchen", "Kitchen"),
                       device("media_player.lounge", "Living Room Speaker"),
                       device("media_player.default", "Default")};
    context.defaultDevice = "media_player.default";
    return context;
}

/**
 * @brief Check that text parses to a media_player command, printing both commands on mismatch.
 */
void expectMedia(RegexParser& parser, const string& text, const string& service, const string& entity,
                 const json& data = json::object()) {
    const auto actual = parser.parse(text, speakers());
    const Command expected{"media_player", service, entity, data};

    if(!actual || *actual != expected) {
        std::cerr << "Expected " << json(expected) << ", got " << (actual ? json(*actual) : json()) << '\n';
    }

    check(actual && *actual == expected, text);
}

/**
 * @brief Entity a parsed command targets, or "<none>" when nothing was parsed.
 */
string target(const std::optional<Command>& command) {
    return command ? command->entity : "<none>";
}
} // namespace

/**
 * @brief Run grammar, spoken-number, name-resolution and ambiguity regressions.
 */
int main() {
    RegexParser parser(readTestConfiguration());
    const string kitchen = "media_player.kitchen";
    const string lounge = "media_player.lounge";
    const string fallback = "media_player.default";

    for(const auto& text :
        {"set the volume of the living room speaker to forty", "set the living room speaker volume to 40",
         "set the living room speaker's volume to forty percent",
         "change volume on living room speaker to 40%",
         "adjust the volume for the living room speaker at forty",
         "put volume in living room speaker to forty", "volume of living room speaker 40",
         "living room speaker volume 40", "set volume to forty on the living room speaker",
         "set the volume to forty for living room speaker", "turn living room speaker to 0.4",
         "Could you please set the volume of the living room speaker to forty, please?",
         "set the volumn of living room speaker to forty"}) {
        expectMedia(parser, text, "volume_set", lounge, {{"volume_level", 0.4}});
    }

    expectMedia(parser, "turn the kitchen volume up by ten", "volume_up", kitchen, {{"amount", .1}});
    expectMedia(parser, "turn the kitchen volume down", "volume_down", kitchen);
    expectMedia(parser, "volume zero point four", "volume_set", fallback, {{"volume_level", .4}});
    expectMedia(parser, "volume point five", "volume_set", fallback, {{"volume_level", .5}});
    expectMedia(parser, "volume one point zero", "volume_set", fallback, {{"volume_level", 1.0}});
    expectMedia(parser, "volume twenty-one", "volume_set", fallback, {{"volume_level", .21}});
    expectMedia(parser, "set volume to 1", "volume_set", fallback, {{"volume_level", .01}});
    expectMedia(parser, "set volume to 1.0", "volume_set", fallback, {{"volume_level", 1.0}});
    expectMedia(parser, "set volume to one hundred percent", "volume_set", fallback, {{"volume_level", 1.0}});
    expectMedia(parser, "set kitchen to full volume", "volume_set", kitchen, {{"volume_level", 1.0}});
    expectMedia(parser, "set volume to half", "volume_set", fallback, {{"volume_level", .5}});
    expectMedia(parser, "set volume to zero", "volume_set", fallback, {{"volume_level", 0}});

    for(const auto& text :
        {"turn kitchen up by ten", "raise kitchen volume by ten percent", "increase volume on kitchen by ten",
         "turn volume up on kitchen by ten", "volume up in kitchen by ten", "make kitchen louder by ten"}) {
        expectMedia(parser, text, "volume_up", kitchen, {{"amount", .1}});
    }

    for(const auto& text : {"turn kitchen down by ten", "lower kitchen volume by ten percent",
                            "decrease volume on kitchen by ten", "turn volume down on kitchen by ten",
                            "volume down in kitchen by ten", "make kitchen quieter by ten"}) {
        expectMedia(parser, text, "volume_down", kitchen, {{"amount", .1}});
    }

    for(const auto& text : {"turn the volume up", "turn up", "volume up", "raise volume"}) {
        expectMedia(parser, text, "volume_up", fallback);
    }

    expectMedia(parser, "turn volume down by five", "volume_down", fallback, {{"amount", .05}});
    expectMedia(parser, "turn up by ten", "volume_up", fallback, {{"amount", .1}});
    expectMedia(parser, "turn kitchen down", "volume_down", kitchen);
    expectMedia(parser, "mute the kitchen", "volume_mute", kitchen, {{"is_volume_muted", true}});
    expectMedia(parser, "unmute the sound on the kitchen", "volume_mute", kitchen,
                {{"is_volume_muted", false}});
    expectMedia(parser, "kitchen mute", "volume_mute", kitchen, {{"is_volume_muted", true}});
    expectMedia(parser, "mute", "volume_mute", fallback, {{"is_volume_muted", true}});
    expectMedia(parser, "play the music in the kitchen", "media_play", kitchen); // "play ... on" is a Spotify cast
    expectMedia(parser, "resume kitchen", "media_play", kitchen);
    expectMedia(parser, "pause", "media_pause", fallback);
    expectMedia(parser, "pause music in kitchen", "media_pause", kitchen);
    expectMedia(parser, "kitchen pause", "media_pause", kitchen);
    expectMedia(parser, "stop playback on kitchen", "media_stop", kitchen);
    expectMedia(parser, "toggle music on kitchen", "media_play_pause", kitchen);
    expectMedia(parser, "skip to the next track on kitchen", "media_next_track", kitchen);
    expectMedia(parser, "skip this song", "media_next_track", fallback);
    expectMedia(parser, "next song on kitchen", "media_next_track", kitchen);
    expectMedia(parser, "go back one song in kitchen", "media_previous_track", kitchen);
    expectMedia(parser, "turn on the kitchen", "turn_on", kitchen);
    expectMedia(parser, "switch the kitchen off", "turn_off", kitchen);

    for(const auto& text :
        {"", "hello", "set volume to 101", "set volume to -10", "set volume to - 10",
         "set volume to minus ten", "set volume to twenty fifty", "set volume to ten cats",
         "volume one point five", "volume zero point twenty", "don't pause kitchen", "do not mute kitchen",
         "pause kitchen and play bedroom", "set volume to 99999999999999999999999999999999",
         "turn kitchen up by 101 percent"}) {
        check(!parser.parse(text, speakers()), string("rejects: ") + text);
    }

    check(!parser.parse(string(513, 'x'), {}), "length limit");

    Context names;
    names.devices = {device("media_player.kitchen", "Kitchen"),
                     device("media_player.lounge", "Living Room Speaker")};
    check(target(parser.parse("pause the kitchen", names)) == kitchen, "exact name");
    check(target(parser.parse("pause kichen", names)) == kitchen, "one-edit fuzzy name");
    check(target(parser.parse("pause living rom speker", names)) == lounge, "two-edit fuzzy name");
    check(!parser.parse("pause unknown", names), "unknown device");
    names.fuzzyNames = false;
    check(!parser.parse("pause kichen", names), "fuzzy matching disabled");
    names.fuzzyNames = true;
    names.defaultDevice = kitchen;
    check(parser.parse("pause", names) == Command{"media_player", "media_pause", kitchen, json::object()},
          "default device");
    check(parser.parse("Please turn kitchen up by ten percent!", names) ==
              Command{"media_player", "volume_up", kitchen, {{"amount", 0.1}}},
          "relative volume is a structured amount");
    names.defaultDevice = "climate.ac";
    check(target(parser.parse("pause", names)).empty(), "a default device from another domain is never used");
    check(target(parser.parse("pause", {})).empty(), "an unnamed media target stays empty for the executor");
    check(parser.parse("turn up by ten", {}) == Command{"media_player", "volume_up", "", {{"amount", 0.1}}},
          "relative command keeps an empty target for the executor");

    Context ambiguous;
    ambiguous.devices = {device("media_player.a", "kitchan"), device("media_player.b", "kitchon")};
    check(!parser.parse("pause kitchen", ambiguous),
          "equally close names of different devices are ambiguous");
    ambiguous.devices.pop_back();
    const Grammar::DeviceNames sameEntity(ambiguous, {{"media_player.a", json::array({"kitchon"})}});
    check(sameEntity.resolve("kitchen", "media_player") == string("media_player.a"),
          "equally close names of the same device resolve");

    Context discovered;
    discovered.devices = {
        device("media_player.rumpus_room_q990b", "Q990"), device("light.q990", "Q990"),
        device("media_player.first", " The Kitchen "),    device("media_player.second", "kitchen"),
        device("media_player.third", "KITCHEN's"),        device("media_player.first", " The Kitchen ")};
    const Grammar::DeviceNames discoveredNames(discovered, json::object());
    check(discoveredNames.resolve("rumpus room q990b", "media_player") ==
              string("media_player.rumpus_room_q990b"),
          "object ID with spaces is a name");
    check(discoveredNames.resolve("media_player.rumpus_room_q990b", "media_player") ==
              string("media_player.rumpus_room_q990b"),
          "full entity ID is a name");
    check(
        parser.parse("set the volume on the Q990 to 0.2.", discovered) ==
            Command{"media_player", "volume_set", "media_player.rumpus_room_q990b", {{"volume_level", 0.2}}},
        "names are scoped to the command's domain");
    check(!parser.parse("pause kitchen", discovered), "a name shared by different devices is blocked");
    check(!parser.parse("pause kichen", discovered), "a blocked name is not matched fuzzily");
    check(target(parser.parse("pause first", discovered)) == "media_player.first",
          "a repeated device keeps its unique names");
    check(stripWakeWord(" Jarvis, turn on the Q990.", "jarvis") == string("turn on the q990"), "wake word opens a command");
    check(stripWakeWord("Hey Jarvis turn off the light", "jarvis") == string("turn off the light"), "greeting before the wake word");
    check(stripWakeWord("Travis pause the kitchen", "jarvis") == string("pause the kitchen"), "misheard wake word");
    check(stripWakeWord("ok jar vis pause", "jarvis") == string("pause"), "split wake word");
    check(stripWakeWord("hey house lights off", "hey house") == string("lights off"), "multi-word wake phrase");
    check(stripWakeWord("Jarvis.", "jarvis") == string(), "wake word alone");
    check(!stripWakeWord("turn on the Q990", "jarvis"), "speech without the wake word is ignored");
    check(!stripWakeWord("pause jarvis", "jarvis"), "the wake word must come first");
    check(!stripWakeWord("hey there turn it off", "jarvis"), "a different name is ignored");
    check(stripWakeWord(" Pause.", "") == string(" Pause."), "no wake word accepts everything");

    for(const auto* sound : {" Hahaha", " Ha ha.", "hehe", " Mhm.", "Mm-hmm", "Hmmm...", " Uh,", "Um", "Uh huh", "ohh", "Ahh!", "(laughs)", " [MUSIC]", "*sighs* hmm"}) {
        check(isNonSpeech(sound), string("non-speech: ") + sound);
    }

    for(const auto* speech : {" Pause.", "Play", " Hey Jarvis", "AC", "Um, turn on the light", "oh pause it", "Home", "Hold"}) {
        check(!isNonSpeech(speech), string("speech: ") + speech);
    }

    // Spotify phrasings: each family resolves to one service, and device-less services take no device.
    const Context music{{device("spotify.speaker", "Q990B"), device("spotify.phone", "SM-S928B"), device("media_player.q990", "Q990B")},
                        Spotify("http://127.0.0.1:1").discoverServices()};
    const Command cast{"spotify", "cast", "spotify.speaker", json::object()};

    for(const auto* text : {"cast to the q990b", "switch to the q990b", "play music on the q990b", "move the music to the q990b",
                            "playback on the q990b", "start playback on the q990b", "transfer playback to the q990b"}) {
        check(parser.parse(text, music) == cast, string("cast: ") + text);
    }

    const Command wait{"spotify", "time_until", "", {{"song", "bohemian rhapsody"}}};

    for(const auto* text : {"time until bohemian rhapsody", "time till bohemian rhapsody", "when is bohemian rhapsody playing",
                            "when's bohemian rhapsody on", "how long until bohemian rhapsody", "how long before bohemian rhapsody comes on"}) {
        check(parser.parse(text, music) == wait, string("time until: ") + text);
    }

    const Command request{"spotify", "request_song", "", {{"song", "wonderwall"}}};

    for(const auto* text : {"add wonderwall to the queue", "add wonderwall to queue", "add wonderwall", "queue up wonderwall", "play wonderwall"}) {
        check(parser.parse(text, music) == request, string("request: ") + text);
    }

    for(const auto* text : {"what's in the queue", "what's next", "what's playing next", "show me the queue"}) {
        check(parser.parse(text, music) == Command{"spotify", "get_queue", "", json::object()}, string("queue: ") + text);
    }

    check(parser.parse("pause the q990b", music) == Command{"media_player", "media_pause", "media_player.q990", json::object()},
          "playback controls stay with Home Assistant");
    check(!parser.parse("play the q990b", music), "playing a speaker that shares a name with a song request is left to a fallback");
    check(!parser.parse("add wonderwall", Context{music.devices}), "Spotify phrases match nothing without the Spotify catalogue");

    const auto replyOf = [&](const char* reply, const char* text) {
        RegexParser worded(json{{"version", 1}, {"rules", {{{"id", "cast"}, {"domain", "spotify"}, {"service", "cast"}, {"reply", reply}, {"phrases", {"cast to {device}", "cast"}}}}}});
        const auto command = worded.parse(text, music);
        return command ? command->reply : string("no command");
    };
    check(replyOf("Casting to {device}.", "cast to q990b") == "Casting to q990b.", "a rule's reply takes captured words");
    check(replyOf("Done.", "cast to q990b") == "Done.", "a reply need not use a slot");
    check(replyOf("Casting to {speaker}.", "cast to q990b") == "", "a reply naming an uncaptured slot is dropped");

    /*
     * @brief A parser that always proposes the same command, standing in for a language model.
     */
    struct Fixed final : Commands::Parser {
        void configure(const json&) override {}
        std::optional<Command> parse(const string&, const Context&) override { return Command{"media_player", "media_play", "media_player.q990", json::object()}; }
    } model;
    FallbackParser hybrid(parser, model);
    check(hybrid.failureReason().empty(), "a parser with nothing to report gives no reason");
    check(hybrid.parse("add wonderwall", music) == request, "the hybrid parser prefers phrase rules");
    check(hybrid.parse("play the q990b", music)->service == "media_play", "the hybrid parser falls back when no rule decides");

    std::cout << checks << " parser checks passed\n";
}