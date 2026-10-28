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
                           "# comment\nHOME_ASSISTANT_TOKEN=\"test-file-token\"\n";
    unsetenv("HOME_ASSISTANT_URL");
    unsetenv("HOME_ASSISTANT_TOKEN");
    auto connection = readHomeAssistantSettings(file.string());
    check(connection.url == "http://example.invalid:8123" && connection.token == "test-file-token",
          "file values are read and unquoted");
    check(!std::getenv("HOME_ASSISTANT_URL") && !std::getenv("HOME_ASSISTANT_TOKEN"),
          "reading settings does not change the environment");
    setenv("HOME_ASSISTANT_TOKEN", "environment-token", 1);
    connection = readHomeAssistantSettings(file.string());
    check(connection.token == "environment-token" && connection.url == "http://example.invalid:8123",
          "the environment overrides file values");
    std::filesystem::remove(file);

    check(parse({"voice-test"}).mode == Mode::Listen, "listening is the default mode");
    check(parse({"voice-test", "--help"}).mode == Mode::Help, "help");
    const auto preview = parse({"voice-test", "--parse", "pause kitchen", "--cpu", "model.bin"});
    check(preview.mode == Mode::Parse && preview.parseText == "pause kitchen" && !preview.gpu &&
              preview.model == "model.bin",
          "options combine with one mode");
    checkThrows([] { parse({"voice-test", "--list-devices", "--parse", "pause kitchen"}); },
                "conflicting modes are rejected");
    checkThrows([] { parse({"voice-test", "--threads", "4x"}); }, "partial numbers are rejected");
    checkThrows([] { parse({"voice-test", "--threads", "0"}); }, "out-of-range values are rejected");
    checkThrows([] { parse({"voice-test", "--phrases"}); }, "a missing value is rejected");
    checkThrows([] { parse({"voice-test", "a.bin", "b.bin"}); }, "only one model path is accepted");
    std::cout << checks << " settings checks passed\n";
}