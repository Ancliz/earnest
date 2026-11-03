/**
 * @file
 * @brief Regression suite for bounded YAML decoding and transactional hot reload.
 */

#include "check.hpp"
#include "config/configuration.hpp"
#include "parser/regex_parser.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <unistd.h>

using Commands::Command;
using Commands::Context;
using Commands::json;
using Config::readYaml;
using std::string;

/**
 * @brief Verify reload, rejection, rollback across consumers, and YAML limits.
 */
int main() {
    const auto directory =
        std::filesystem::temp_directory_path() / ("smarthome-config-" + std::to_string(getpid()));
    std::filesystem::create_directory(directory);
    const string file = (directory / "phrases.yaml").string();
    /*
     * @brief Replace the test phrase file.
     */
    const auto write = [&](const string& contents) { std::ofstream(file) << contents; };
    const string pauseRule = "version: 1\nrules:\n  - id: pause\n    domain: media_player\n"
                             "    service: media_pause\n    phrases: ['hold [the] {device}']\n";
    Context context;
    context.devices = {{"media_player.kitchen", "Kitchen", "media_player", "playing", json::object()}};
    const Command pause{"media_player", "media_pause", "media_player.kitchen", json::object()};
    RegexParser parser;
    json recorded;
    write(pauseRule);
    // The recorder accepts everything before the parser can reject, exercising rollback.
    Config::Configuration configuration(file, [&](const json& candidate) {
        recorded = candidate;
        parser.configure(candidate);
    });

    check(parser.parse("hold kitchen", context) == pause, "initial configuration applied");
    check(parser.parse("hold the kitchen", context) == pause, "optional determiner");
    write("version: 1\nrules: []\n");
    check(parser.parse("hold kitchen", context) == pause, "parsing never reads the file");
    checkThrows([&] { configuration.reloadIfChanged(); }, "rejected edit throws");
    check(recorded == configuration.data() && parser.parse("hold kitchen", context) == pause,
          "rejected edit rolls every consumer back");
    check(!configuration.reloadIfChanged(), "rejected text is not retried until edited");
    write("version: [broken");
    checkThrows([&] { configuration.reloadIfChanged(); }, "invalid YAML rejected");
    write(pauseRule + "    body: {volume_level: '{missing}'}\n");
    checkThrows([&] { configuration.reloadIfChanged(); }, "template slot without a capture rejected");
    write(pauseRule + "    body: {}\n");
    check(configuration.reloadIfChanged() && recorded == configuration.data(), "valid edit applied");

    // An installation file overlays the phrase file: objects merge, lists append, scalars replace.
    const string home = (directory / "home.yaml").string();
    Config::Configuration overlaid(file, [&](const json& candidate) { recorded = candidate; }, home);
    check(!recorded.contains("aliases"), "a missing home file is optional");
    std::ofstream(home) << "aliases:\n  media_player.kitchen: [cooking]\nrules:\n  - id: extra\n";
    check(overlaid.reloadIfChanged() && recorded["aliases"]["media_player.kitchen"][0] == "cooking" && recorded["rules"].size() == 2,
          "a home file change reloads and merges");
    check(Config::mergeConfiguration({{"a", {{"b", 1}, {"c", 2}}}}, {{"a", {{"c", 3}}}}) == json{{"a", {{"b", 1}, {"c", 3}}}},
          "nested objects merge");
    std::filesystem::remove(home);
    std::filesystem::remove(file);
    checkThrows([&] { configuration.reloadIfChanged(); }, "deleted file reported");
    check(parser.parse("hold kitchen", context) == pause, "deleted file keeps the active rules");
    write(pauseRule + "    body: {}\n");
    check(!configuration.reloadIfChanged(), "restoring the active text is not a change");

    checkThrows([] { readYaml("version: 1\nversion: 2\n"); }, "duplicate YAML keys rejected");
    checkThrows([] { readYaml("a: 1\n---\nb: 2\n"); }, "extra YAML documents rejected");
    string expansion = "a: &a [0, 0, 0, 0, 0, 0, 0, 0]\n";

    // Each line repeats the previous alias eight times, so six lines expand to over 32768 nodes.
    for(char current = 'b'; current <= 'f'; ++current) {
        expansion += string(1, current) + ": &" + current + " [";

        for(int i = 0; i < 8; ++i) {
            expansion += (i ? ", *" : "*") + string(1, current - 1);
        }

        expansion += "]\n";
    }

    checkThrows([&] { readYaml(expansion); }, "alias expansion is bounded");
    std::filesystem::remove_all(directory);
    std::cout << checks << " configuration checks passed\n";
}