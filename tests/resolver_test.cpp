/**
 * @file
 * @brief Regression suite for the device registry and the resolver every parser shares.
 */

#include "check.hpp"
#include "home/resolver.hpp"
#include "parser/regex_parser.hpp"

#include <iostream>

using Commands::Context;
using Commands::Device;
using Home::Reach;
using Home::Registry;
using Home::Resolver;
using Outcome = Home::Resolution::Outcome;
using std::string;

namespace {
/**
 * @brief A device with the given entity ID and friendly name.
 */
Device device(const string& entity, const string& name) {
    return {entity, name, entity.substr(0, entity.find('.')), "on", json::object()};
}

/**
 * @brief A configuration with a soundbar known to two executors and a room to put it in.
 */
json house() {
    return {{"rooms", {{"rumpus_room", json::array({"tv room"})}, {"kitchen", json::array()}}}, {"aliases", {{"spotify.abc", json::array({"big speaker"})}}},
            {"devices", {{"sound_system", {{"room", "rumpus_room"}, {"aliases", json::array({"sound bar"})}, {"entities", json::array({"media_player.q990b", "spotify.abc"})}}}}}};
}
} // namespace

/**
 * @brief Verify configuration checks, name sharing within a device, and how ties are settled.
 */
int main() {
    const Registry registry(house());
    Context context({device("media_player.q990b", "Q990B"), device("spotify.abc", "Samsung Living"), device("media_player.kitchen", "Kitchen"), device("media_player.kitchen_speaker", "Kitchen Speaker")});
    const Resolver resolver(registry, context);

    check(resolver.resolve("q990b", "spotify").entity == "spotify.abc", "a name from one executor reaches the device's entity in another");
    check(resolver.resolve("samsung living", "media_player").entity == "media_player.q990b", "and the other way round");
    check(resolver.resolve("big speaker", "media_player").entity == "media_player.q990b", "an entity's alias belongs to its whole device");
    check(resolver.resolve("sound bar", "spotify").entity == "spotify.abc", "a device's alias reaches each entity");
    check(resolver.resolve("sound system", "media_player").entity == "media_player.q990b", "the device's key counts as a name, with underscores as spaces");
    check(!resolver.resolve("sound bar", "light"), "a domain the device isn't in has no match");
    check(!resolver.resolve("rumpus room", "media_player"), "a room isn't a device name");
    check(resolver.resolve("rumpus sound system", "media_player", Reach::Words).entity == "media_player.q990b", "a room's words help matching by words");
    check(resolver.resolve("kitchen", "media_player", Reach::Words).entity == "media_player.kitchen", "an exact name beats a partial one");
    check(resolver.resolve("kitchen speaker", "media_player", Reach::Words, "media_player.kitchen").entity == "media_player.kitchen_speaker", "the best match replaces a wrong preference");
    check(registry.aliasesOf("spotify.abc") == std::vector<string>({"big speaker", "sound system", "sound bar"}), "aliases include the physical device's");
    check(registry.aliasesOf("media_player.q990b") == registry.aliasesOf("spotify.abc"), "every entity of a device has the same aliases");

    Context following = context;
    following.defaultDevice = "spotify.abc";
    const Resolver follower(registry, following);
    check(follower.defaultIn("spotify") == "spotify.abc", "the default device is used in its own domain");
    check(follower.defaultIn("media_player") == "media_player.q990b", "and reaches the same physical device in another");
    check(follower.defaultIn("light").empty(), "but not a domain the device isn't in");

    Context spotifyGone({device("media_player.q990b", "Q990B")});
    check(Resolver(registry, spotifyGone).resolve("big speaker", "media_player").entity == "media_player.q990b", "an absent entity doesn't take the device with it");
    check(Resolver(registry, Context()).resolve("sound bar", "media_player").outcome == Outcome::Unknown, "a device with no entity present is unknown");

    Context wrongDomain({device("media_player.desk", "Desk Speaker"), device("light.desk", "Desk Lamp")});
    check(Resolver(Registry(), wrongDomain).resolve("desk lamp", "media_player", Reach::Words).outcome == Outcome::OtherDomain, "a name matching better in another domain says so");
    check(Resolver(Registry(), wrongDomain).resolve("garage", "media_player", Reach::Words).outcome == Outcome::Unknown, "an unknown name says so");

    json roomy = {{"rooms", {{"lounge", json::array()}, {"study", json::array()}}},
                  {"devices", {{"lounge_lamp", {{"room", "lounge"}, {"entities", json::array({"light.one"})}}}, {"study_lamp", {{"room", "study"}, {"entities", json::array({"light.two"})}}}}}};
    Context lamps({device("light.one", "Lamp"), device("light.two", "Lamp")});
    check(Resolver(Registry(roomy), lamps).resolve("lamp", "light").outcome == Outcome::Ambiguous, "two devices with one name are ambiguous");
    check(Resolver(Registry(roomy), lamps).resolve("lamp", "light", Reach::Words, "light.two").entity == "light.two", "a preference settles a tie");
    lamps.room = "study";
    check(Resolver(Registry(roomy), lamps).resolve("lamp", "light").entity == "light.two", "the room settles a tie");
    check(Resolver(Registry(roomy), lamps).resolve("lamp", "light", Reach::Names, "light.one").entity == "light.two", "the room outranks a preference");

    json twice = {{"devices", {{"bar", {{"entities", json::array({"media_player.cast", "media_player.samsung"})}}}}}};
    Context both({device("media_player.samsung", "Samsung"), device("media_player.cast", "Cast")});
    check(Resolver(Registry(twice), both).resolve("bar", "media_player").entity == "media_player.cast", "the first listed entity of a domain is used");
    check(Resolver(Registry(twice), both).resolve("bar", "media_player", Reach::Names, "media_player.samsung").entity == "media_player.samsung", "unless another of the device's entities is preferred");

    checkThrows([] { Registry(json{{"devices", {{"bar", {{"entities", json::array()}}}}}}); }, "a device needs entities");
    checkThrows([] { Registry(json{{"devices", {{"bar", {{"entities", json::array({"nodot"})}}}}}}); }, "entities need a domain");
    checkThrows([] { Registry(json{{"devices", {{"bar", {{"entitys", json::array({"a.b"})}}}}}}); }, "a misspelt setting is rejected");
    checkThrows([] { Registry(json{{"devices", {{"one", {{"entities", json::array({"a.b"})}}}, {"two", {{"entities", json::array({"a.b"})}}}}}}); }, "an entity can't be in two devices");
    checkThrows([] { Registry(json{{"aliases", {{"nodot", json::array({"x"})}}}}); }, "aliases need an entity ID");
    checkThrows([] { Registry(json{{"aliases", {{"a.b", json::array({""})}}}}); }, "aliases can't be empty");
    checkThrows([] { Registry(json{{"rooms", json::array()}}); }, "rooms must be a map");

    const auto spotify = [](const char* entity, const char* name) { auto found = device(entity, name); found.sameNameDomain = "media_player"; return found; };
    const auto placed = [](const char* entity, const char* name, const char* room, const char* hardware) { auto found = device(entity, name); found.room = room; found.hardwareId = hardware; return found; };

    Context linked({device("media_player.q990b", "Q990B"), spotify("spotify.abc", "Q990B"), spotify("spotify.phone", "S24")});
    const json phoneless = {{"aliases", {{"spotify.abc", json::array({"big speaker"})}}}};
    check(Resolver(Registry(phoneless), linked).resolve("big speaker", "media_player").entity == "media_player.q990b", "a Spotify speaker links to the same-named media player without configuration");
    check(!Resolver(Registry(), linked).resolve("s24", "media_player"), "a Spotify device with no same-named media player stays alone");
    linked.devices.push_back(device("media_player.other", "Q990B"));
    check(!Resolver(Registry(phoneless), linked).resolve("big speaker", "media_player"), "a name fitting two media players links nothing");
    linked.devices.pop_back();
    const json alone = {{"aliases", {{"spotify.abc", json::array({"big speaker"})}}}, {"devices", {{"spotify_bar", {{"entities", json::array({"spotify.abc"})}}}}}};
    check(!Resolver(Registry(alone), linked).resolve("big speaker", "media_player"), "listing an entity in the configuration overrides a discovered link");

    Context zones({placed("climate.ac", "AC", "", "hub"), placed("climate.bed", "Master Bed", "", "hub"), placed("switch.ac_fan", "My Fan", "", "hub")});
    check(Resolver(Registry(), zones).resolve("master bed", "climate").entity == "climate.bed", "hardware with several entities in a domain keeps them apart");
    check(!Resolver(Registry(), zones).resolve("my fan", "climate"), "and doesn't share their names");
    Context lamp({placed("light.ceiling", "Ceiling", "", "fan"), placed("fan.blades", "Blades", "", "fan")});
    check(Resolver(Registry(), lamp).resolve("blades", "light").entity == "light.ceiling", "hardware with one entity per domain is one device");

    Context areas({placed("light.one", "Lamp", "Lounge", ""), placed("light.two", "Lamp", "Study", "")});
    areas.room = "study";
    check(Resolver(Registry(), areas).resolve("lamp", "light").entity == "light.two", "a discovered area settles a tie, however it is capitalised");
    const json twoWords = {{"devices", {{"lamp_two", {{"room", "the_den"}, {"entities", json::array({"light.two"})}}}}}};
    areas.room = "the den";
    check(Resolver(Registry(twoWords), areas).resolve("lamp", "light").entity == "light.two", "a room written with underscores is the same room");
    areas.room = "den";
    check(Resolver(Registry(), areas).resolve("lamp", "light").outcome == Outcome::Ambiguous, "a room with no match settles nothing");
    const json moved = {{"devices", {{"lamp_two", {{"room", "den"}, {"entities", json::array({"light.two"})}}}}}};
    check(Resolver(Registry(moved), areas).resolve("lamp", "light").entity == "light.two", "a configured room overrides the discovered area");
    areas.room = "";
    const json named = {{"rooms", {{"Study", json::array({"office"})}}}};
    check(Resolver(Registry(named), areas).resolve("office lamp", "light", Reach::Words).entity == "light.two", "configured room names apply to a discovered area");

    json castRule = {{"id", "cast"}, {"domain", "spotify"}, {"service", "cast"}, {"phrases", json::array({"cast to {device}"})}};
    json config = house();
    config["version"] = 1;
    config["rules"] = json::array({castRule});
    RegexParser parser(config);
    const auto cast = parser.parse("cast to the q990b", context);
    check(cast && cast->entity == "spotify.abc", "a rule reaches a device through a name only another executor knows");

    std::cout << checks << " resolver checks passed\n";
}