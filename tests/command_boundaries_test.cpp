/**
 * @file
 * @brief Regression suite for command boundaries behavior.
 */

#include "commands/command.hpp"
#include "config/configuration.hpp"
#include "parser/parser.hpp"
#include "app/settings.hpp"
#include "audio/utterance_buffer.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <unistd.h>

using Commands::json;
using std::string;

namespace {
/**
 * @brief Require one architectural boundary invariant to hold.
 *
 * @param[in] passed Whether the observed behavior satisfies the invariant.
 * @param[in] message Description copied into a failure exception.
 * @throws std::runtime_error If passed is false.
 */
void check(bool passed, const string& message) {
    if(!passed) {
        throw std::runtime_error(message);
    }
}

class AlternateParser final : public Commands::Parser {
public:
    /**
     * @brief Accept configuration without changing the alternate parser test double.
     *
     * The unnamed configuration argument is intentionally ignored. This backend has
     * one fixed result and exists solely to exercise polymorphic parser selection.
     */
    void configure(const json&) override {}

    /**
     * @brief Return a fixed stop command through the same interface as the regex backend.
     *
     * Both unnamed arguments, transcript and context, are intentionally ignored.
     *
     * @return A media_stop request targeting the fixture's kitchen entity.
     * @note Performs no parsing, HTTP, file access, or command execution.
     */
    json parse(const string&, const Commands::Context&) const override {
        return {{"endpoint", "/services/media_player/media_stop"},
                {"body", {{"entity_id", "media_player.kitchen"}}}};
    }
};

struct TemporaryDirectory {
    std::filesystem::path path =
        std::filesystem::temp_directory_path() / ("smarthome-boundaries-" + std::to_string(getpid()));

    /**
     * @brief Create the per-process scratch directory used by the boundary suite.
     *
     * @throws std::filesystem::filesystem_error If directory creation fails.
     * @note Writes to the system temporary directory; fixture paths are stored beneath it.
     */
    TemporaryDirectory() {
        std::filesystem::create_directory(path);
    }

    /**
     * @brief Recursively remove the suite's scratch path and its fixture files.
     *
     * @note Performs filesystem deletion during scope cleanup. The throwing filesystem
     *       overload is used, so a cleanup error during destruction is fatal.
     */
    ~TemporaryDirectory() {
        std::filesystem::remove_all(path);
    }
};
} // namespace

/**
 * @brief Verify parser interchangeability and the absence of hidden boundary side effects.
 *
 * Checks explicit versus implicit reloads, parser-instance isolation, failed-candidate
 * preservation, bounded YAML expansion, dotenv environment isolation, CLI-mode
 * conflicts, and audio discard events without stderr output.
 *
 * @return Zero when every invariant holds.
 * @throws std::runtime_error For failed assertions or invalid fixture operations.
 * @note Reads/writes temporary files and temporarily redirects stderr; never acquires
 *       a microphone, loads Whisper, contacts Home Assistant, or executes a device action.
 */
int main() {
    TemporaryDirectory temporary;
    const auto file = temporary.path / "phrases.yaml";
    /*
     * @brief Overwrite the captured temporary phrase file for a reload scenario.
     *
     * @param[in] contents Complete candidate YAML source.
     * @note Writes the fixture file only; configure/reload operations are tested separately.
     */
    const auto write = [&](const string& contents) { std::ofstream(file) << contents; };
    const string first = "version: 1\nrules:\n  - id: pause\n    domain: media_player\n"
                         "    service: media_pause\n    phrases: ['hold {device}']\n";
    Parser::RegexParser regex;
    Commands::Parser* parser = &regex;
    write(first);
    /*
     * @brief Forward a decoded candidate to whichever parser backend the fixture currently selects.
     *
     * @param[in] candidate Candidate configuration supplied by the explicit file controller.
     * @throws std::exception If the selected backend rejects the candidate.
     * @note The callback captures the parser pointer by reference to exercise interface use.
     */
    Config::Configuration configuration(file.string(),
                                        [&](const json& candidate) { parser->configure(candidate); });
    const Commands::Context context;
    const auto pause = parser->parse("hold kitchen", context);
    check(pause["endpoint"] == "/services/media_player/media_pause",
          "regex backend parses through interface");
    write("version: 1\nrules: []\n");
    check(parser->parse("hold kitchen", context) == pause,
          "parsing never reads or reloads a changed configuration file");
    const auto previous = configuration.data();
    check(!configuration.reloadIfChanged() && !configuration.error().empty(), "explicit bad reload fails");
    check(configuration.data() == previous && parser->parse("hold kitchen", context) == pause,
          "failed reload preserves both parser and policy snapshot");
    Parser::RegexParser independent;
    independent.configure(Config::readYaml("version: 1\nrules:\n  - id: stop\n    domain: media_player\n"
                                           "    service: media_stop\n    phrases: ['hold {device}']\n"));
    check(independent.parse("hold kitchen")["endpoint"] == "/services/media_player/media_stop" &&
              regex.parse("hold kitchen") == pause,
          "parser instances do not share hidden global rules");
    AlternateParser alternate;
    parser = &alternate;
    check(parser->parse("hold kitchen", context)["endpoint"] == "/services/media_player/media_stop",
          "another backend uses the same parse contract");

    string expansion = "a: &a [0, 0, 0, 0, 0, 0, 0, 0]\n";
    char previousAlias = 'a';

    for(char current = 'b'; current <= 'f'; ++current) {
        expansion += string(1, current) + ": &" + current + " [";

        for(int i = 0; i < 8; ++i) {
            expansion += (i ? ", *" : "*") + string(1, previousAlias);
        }

        expansion += "]\n";
        previousAlias = current;
    }

    bool expansionRejected = false;

    try {
        Config::readYaml(expansion);
    } catch(const std::runtime_error&) {
        expansionRejected = true;
    }

    check(expansionRejected, "small YAML alias inputs cannot expand without a node bound");

    const auto environmentFile = temporary.path / ".env";
    std::ofstream(environmentFile) << " export HOME_ASSISTANT_URL = 'http://example.invalid:8123'\n"
                                      "HOME_ASSISTANT_TOKEN='test-file-token'\n";
    const char* beforeUrl = std::getenv("HOME_ASSISTANT_URL");
    const char* beforeToken = std::getenv("HOME_ASSISTANT_TOKEN");
    const string savedUrl = beforeUrl ? beforeUrl : "";
    const string savedToken = beforeToken ? beforeToken : "";
    const auto connection = readHomeAssistantSettings(environmentFile.string());
    check(connection.url == (beforeUrl ? savedUrl : "http://example.invalid:8123") &&
              connection.token == (beforeToken ? savedToken : "test-file-token"),
          "process environment takes precedence over parsed file values");
    check((std::getenv("HOME_ASSISTANT_URL") != nullptr) == (beforeUrl != nullptr) &&
              (std::getenv("HOME_ASSISTANT_TOKEN") != nullptr) == (beforeToken != nullptr),
          "reading environment-file settings does not add process environment variables");
    if(beforeUrl) {
        check(string(std::getenv("HOME_ASSISTANT_URL")) == savedUrl, "existing URL environment is unchanged");
    }

    if(beforeToken) {
        check(string(std::getenv("HOME_ASSISTANT_TOKEN")) == savedToken,
              "existing token environment is unchanged");
    }

    char executable[] = "voice-test";
    char list[] = "--list-devices";
    char parse[] = "--parse";
    char text[] = "pause kitchen";
    char* arguments[] = {executable, list, parse, text};
    bool rejected = false;

    try {
        parseArguments(4, arguments);
    } catch(const std::runtime_error&) {
        rejected = true;
    }

    check(rejected, "conflicting CLI modes are rejected instead of silently taking precedence");
    UtteranceBuffer buffer(0.01f, 200);
    std::ostringstream diagnostics;
    auto* previousOutput = std::cerr.rdbuf(diagnostics.rdbuf());
    buffer.append(Audio::Samples(Audio::blockSamples, 0.1f), true);
    std::cerr.rdbuf(previousOutput);
    check(diagnostics.str().empty() && buffer.event() == UtteranceBuffer::Event::Discontinuity,
          "audio buffering reports an event without writing to stderr");
    std::cout << "Parser replacement, explicit reload, isolated settings, and audio event checks passed\n";
}
