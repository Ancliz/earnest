/**
 * @file
 * @brief Command-line options and service connection settings.
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

    /// Installation file of aliases, voice scope and AC policy, merged over the phrase file.
    std::string homeFile = Config::defaultHomeFile;
    
    /// Text given to --parse.
    std::string parseText;

    /// Phrase that must open each spoken command; empty accepts every utterance.
    std::string wakeWord;

    /// Lowest log level shown, as named by spdlog.
    std::string logLevel = "info";

    /// Log file appended to alongside the console; empty for console only.
    std::string logFile;
};

/**
 * @brief Parse command-line arguments.
 * @throws std::runtime_error For unknown options, missing or malformed values, out-of-range
 *                            settings, or more than one mode.
 */
Settings parseArguments(int argc, char* argv[]);

/**
 * @brief Where the Home Assistant and Ollama servers are, which differ for every installation.
 */
struct ConnectionSettings {

    /// Home Assistant origin including scheme and port (HOME_ASSISTANT_URL).
    std::string url = "http://homeassistant.local:8123";

    /// Long-lived access token (HOME_ASSISTANT_TOKEN); empty when not configured.
    std::string token;

    /// Ollama origin including scheme and port (OLLAMA_URL).
    std::string ollamaUrl = "http://localhost:11434";

    /// Installed Ollama model name and tag (OLLAMA_MODEL).
    std::string ollamaModel = "qwen3.5:4b";
};

/**
 * @brief Read the connection settings from a dotenv file and the environment.
 *
 * Environment variables override file values, and a missing file is ignored. Lines may use an
 * "export " prefix and quoted values. The environment is never modified.
 *
 * @throws std::runtime_error For a non-comment line without "=".
 */
ConnectionSettings readConnectionSettings(const std::string& path = ".env");

/**
 * @brief Log the supported command-line syntax.
 */
void printUsage(const std::string& executable);