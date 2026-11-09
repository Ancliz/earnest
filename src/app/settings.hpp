/**
 * @file
 * @brief Command-line options and service connection settings.
 */

#pragma once

#include "config/configuration.hpp"
#include <algorithm>
#include <optional>
#include <string>
#include <thread>

using std::string;

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
 * @brief Which parser turns transcripts into commands.
 */
enum class ParserKind {
    /// An Ollama language model.
    Ollama,
    /// Phrase rules from the configuration files; needs no model server.
    Regex,
    /// Phrase rules first, and the Ollama model for text no rule decides.
    Hybrid
};

/**
 * @brief The parser named "hybrid", "ollama" or "regex".
 * @throws std::runtime_error For any other name.
 */
ParserKind parserKind(const string& name);

/**
 * @brief Options parsed from the command line.
 */
struct Settings {

    /// Selected mode.
    Mode mode = Mode::Listen;
    
    /// Parser selected with --parser; unset defers to PARSER in the connection settings.
    std::optional<ParserKind> parser;

    /// Ollama model given with --ollama-model; empty defers to OLLAMA_MODEL.
    string ollamaModel;

    /// Whether to send Ollama's prerequest, which is specific to qwen3.5:4b; cleared by --no-prerequest.
    bool prerequest = true;

    /// Whisper GGML model path.
    string model = "models/ggml-base.en.bin";

    /// Transcription threads, 1..256.
    int threads = static_cast<int>(std::max(1u, std::min(4u, std::thread::hardware_concurrency())));

    /// RMS level treated as speech, between 0 and 1.
    float threshold = 0.01f;

    /// Silence that ends an utterance, 200..3000 ms.
    int silenceMs = 700;

    /// Whether to request GPU transcription.
    bool gpu = true;

    /// Phrase, alias and policy file.
    string phraseFile = Config::defaultPhraseFile;

    /// Installation file of aliases, voice scope and AC policy, merged over the phrase file.
    string homeFile = Config::defaultHomeFile;
    
    /// Text given to --parse.
    string parseText;

    /// Phrase that must open each spoken command; empty accepts every utterance.
    string wakeWord;

    /// Lowest log level shown, as named by spdlog.
    string logLevel = "info";

    /// Log file appended to alongside the console; empty for console only.
    string logFile;
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
    string url = "http://homeassistant.local:8123";

    /// Long-lived access token (HOME_ASSISTANT_TOKEN); empty when not configured.
    string token;

    /// Ollama origin including scheme and port (OLLAMA_URL).
    string ollamaUrl = "http://localhost:11434";

    /// Installed Ollama model name and tag (OLLAMA_MODEL).
    string ollamaModel = "qwen3.5:4b";

    /// Spotify control server origin (SPOTIFY_URL); empty leaves Spotify commands out.
    string spotifyUrl;

    /// Chatterbox speech server origin including scheme and port (CHATTERBOX_URL).
    string chatterboxUrl = "http://localhost:4321";

    /// Voice the speech server should use (CHATTERBOX_VOICE).
    string chatterboxVoice = "jarvis";

    /// Parser used when --parser is not given (PARSER): "hybrid", "ollama" or "regex".
    string parser = "hybrid";
};

/**
 * @brief Read the connection settings from a dotenv file and the environment.
 *
 * File values override environment variables, and a missing file is ignored. Lines may use an
 * "export " prefix and quoted values. The environment is never modified.
 *
 * @throws std::runtime_error For a non-comment line without "=".
 */
ConnectionSettings readConnectionSettings(const string& path = ".env");

/**
 * @brief Log the supported command-line syntax.
 */
void printUsage(const string& executable);