/**
 * @file
 * @brief Command-line options and Home Assistant connection settings.
 */

#pragma once

#include "config/configuration.hpp"
#include <algorithm>
#include <string>
#include <thread>

/**
 * @brief What the application does after startup.
 */
enum class Mode {
    /// Capture speech and execute commands.
    Listen,
    /// Print usage.
    Help,
    /// Validate the phrase file and exit.
    CheckPhrases,
    /// Print controllable devices and exit.
    ListDevices,
    /// Preview the command parsed from --parse text without sending it.
    Parse
};

/**
 * @brief Options parsed from the command line.
 */
struct Settings {

    /// Selected mode.
    Mode mode = Mode::Listen;
    
    /// Whisper GGML model path.
    std::string model = "models/ggml-base.en.bin";

    /// Transcription threads, 1..256.
    int threads = static_cast<int>(std::max(1u, std::min(4u, std::thread::hardware_concurrency())));

    /// RMS level treated as speech, between 0 and 1.
    float threshold = 0.01f;

    /// Silence that ends an utterance, 200..3000 ms.
    int silenceMs = 700;

    /// Whether to request GPU transcription.
    bool gpu = true;

    /// Phrase, alias and policy file.
    std::string phraseFile = Config::defaultPhraseFile;
    
    /// Text given to --parse.
    std::string parseText;
};

/**
 * @brief Parse command-line arguments.
 * @throws std::runtime_error For unknown options, missing or malformed values, out-of-range
 *                            settings, or more than one mode.
 */
Settings parseArguments(int argc, char* argv[]);

/**
 * @brief Home Assistant connection settings.
 */
struct HomeAssistantSettings {

    /// Origin including scheme and port.
    std::string url = "http://wyvern:8123";

    /// Long-lived access token; empty when not configured.
    std::string token;
};

/**
 * @brief Read HOME_ASSISTANT_URL and HOME_ASSISTANT_TOKEN from a dotenv file and the environment.
 *
 * Environment variables override file values, and a missing file is ignored. Lines may use an
 * "export " prefix and quoted values. The environment is never modified.
 *
 * @throws std::runtime_error For a non-comment line without "=".
 */
HomeAssistantSettings readHomeAssistantSettings(const std::string& path = ".env");

/**
 * @brief Print the supported command-line syntax.
 */
void printUsage(const std::string& executable);