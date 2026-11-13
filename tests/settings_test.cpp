/**
 * @file
 * @brief Regression suite for command-line parsing and connection settings.
 */

#include "check.hpp"
#include "app/settings.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <unistd.h>
#include <vector>

using std::string;

namespace {
/**
 * @brief Parse a command line given as words, starting with the executable name.
 */
Settings parse(std::vector<string> words) {
    std::vector<char*> argv;

    for(auto& word : words) {
        argv.push_back(word.data());
    }

    return parseArguments(static_cast<int>(argv.size()), argv.data());
}
} // namespace

/**
 * @brief Verify dotenv precedence without environment writes, and CLI mode and value validation.
 */
int main() {
    const auto file =
        std::filesystem::temp_directory_path() / ("smarthome-settings-" + std::to_string(getpid()) + ".env");
    std::ofstream(file) << " export HOME_ASSISTANT_URL = 'http://example.invalid:8123'\n"
                           "# comment\nHOME_ASSISTANT_TOKEN=\"test-file-token\"\nOLLAMA_MODEL=test-model\nPARSER=regex\n";
    unsetenv("HOME_ASSISTANT_URL");
    unsetenv("OLLAMA_URL");
    unsetenv("OLLAMA_MODEL");
    unsetenv("PARSER");
    unsetenv("HOME_ASSISTANT_TOKEN");
    auto connection = readConnectionSettings(file.string());
    check(connection.url == "http://example.invalid:8123" && connection.token == "test-file-token",
          "file values are read and unquoted");
    check(connection.ollamaModel == "test-model" && connection.ollamaUrl == "http://localhost:11434", "Ollama settings are read, with defaults");
    check(connection.parser == "regex", "the parser can be chosen in the file");
    check(connection.chatterboxUrl == "http://localhost:4321" && connection.chatterboxVoice == "jarvis", "the speech server has defaults");
    check(!std::getenv("HOME_ASSISTANT_URL") && !std::getenv("HOME_ASSISTANT_TOKEN"),
          "reading settings does not change the environment");
    setenv("HOME_ASSISTANT_TOKEN", "environment-token", 1);
    setenv("OLLAMA_URL", "http://environment.invalid:11434", 1);
    setenv("CHATTERBOX_URL", "http://environment.invalid:4321", 1);
    setenv("CHATTERBOX_VOICE", "narrator", 1);
    connection = readConnectionSettings(file.string());
    check(connection.chatterboxUrl == "http://environment.invalid:4321" && connection.chatterboxVoice == "narrator", "the speech server and voice are configurable");
    unsetenv("CHATTERBOX_URL");
    unsetenv("CHATTERBOX_VOICE");
    check(connection.token == "test-file-token" && connection.ollamaUrl == "http://environment.invalid:11434",
          "file values override the environment, which fills the rest");
    unsetenv("OLLAMA_URL");
    std::filesystem::remove(file);

    check(parse({"earnest"}).mode == Mode::Listen, "listening is the default mode");
    check(parse({"earnest", "--help"}).mode == Mode::Help, "help");
    check(!parse({"earnest"}).parser && parse({"earnest", "--parser", "regex"}).parser == ParserKind::Regex, "the parser is selectable");
    check(parserKind("hybrid") == ParserKind::Hybrid && ConnectionSettings().parser == "hybrid", "hybrid is a parser and the default");
    checkThrows([] { parse({"earnest", "--parser", "gpt"}); }, "an unknown parser is rejected");
    check(parse({"earnest", "--ollama-model", "llama3.2:3b"}).ollamaModel == "llama3.2:3b", "the Ollama model is selectable");
    check(parse({"earnest"}).prerequest && !parse({"earnest", "--no-prerequest"}).prerequest, "the prerequest is on unless turned off");
    check(!parse({"earnest"}).keepLoaded && parse({"earnest", "--keep-loaded"}).keepLoaded, "keeping the model loaded is off unless asked for");
    check(parse({"earnest"}).answerTemperature == 1.0 && parse({"earnest", "--answer-temperature", "1.5"}).answerTemperature == 1.5, "the answer temperature is selectable");
    check(parse({"earnest"}).followUpSeconds == 8 && parse({"earnest", "--follow-up", "0"}).followUpSeconds == 0, "follow-ups last 8 seconds unless changed");
    check(!parse({"earnest"}).gateWhileSpeaking && parse({"earnest", "--gate-while-speaking"}).gateWhileSpeaking, "the microphone stays on while speaking unless gated");
    const auto rolling = parse({"earnest", "--log-file", "earnest.log", "--log-file-level", "debug", "--log-roll-every", "daily", "--log-max-mb", "5", "--log-keep", "2"});
    check(parse({"earnest"}).logRollEvery == "never" && parse({"earnest"}).logMaxMegabytes == 0 && rolling.logRollEvery == "daily" && rolling.logMaxMegabytes == 5 && rolling.logKeep == 2 && rolling.logFileLevel == "debug", "the log file's level, period and size are set separately");
    checkThrows([] { parse({"earnest", "--log-roll-every", "weekly"}); }, "an unknown period is rejected");
    checkThrows([] { parse({"earnest", "--follow-up", "-1"}); }, "a negative follow-up window is rejected");
    checkThrows([] { parse({"earnest", "--answer-temperature", "3"}); }, "an answer temperature above 2 is rejected");
    const auto preview = parse({"earnest", "--parse", "pause kitchen", "--cpu", "model.bin"});
    check(preview.mode == Mode::Parse && preview.parseText == "pause kitchen" && !preview.gpu &&
              preview.model == "model.bin",
          "options combine with one mode");
    checkThrows([] { parse({"earnest", "--list-devices", "--parse", "pause kitchen"}); },
                "conflicting modes are rejected");
    checkThrows([] { parse({"earnest", "--threads", "4x"}); }, "partial numbers are rejected");
    checkThrows([] { parse({"earnest", "--threads", "0"}); }, "out-of-range values are rejected");
    checkThrows([] { parse({"earnest", "--phrases"}); }, "a missing value is rejected");
    checkThrows([] { parse({"earnest", "a.bin", "b.bin"}); }, "only one model path is accepted");
    std::cout << checks << " settings checks passed\n";
}