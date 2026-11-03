/**
 * @file
 * @brief Argument parsing, dotenv-style connection lookup, and usage output.
 */

#include "settings.hpp"

#include <cmath>
#include <cstdlib>
#include <fstream>
#include <spdlog/spdlog.h>
#include <stdexcept>
#include <type_traits>
#include <utility>

using std::runtime_error;
using std::size_t;
using std::string;

namespace {
/**
 * @brief Convert a whole option value to a number; partial numbers such as "4x" are rejected.
 * @throws std::runtime_error If the value is not entirely a number.
 */
template <typename Number> Number toNumber(const string& value, const string& option) {
    size_t consumed = 0;
    Number number{};

    try {
        if constexpr(std::is_integral_v<Number>) {
            number = std::stoi(value, &consumed);
        } else {
            number = std::stof(value, &consumed);
        }
    } catch(const std::exception&) {
        consumed = 0;
    }

    if(value.empty() || consumed != value.size()) {
        throw runtime_error("Invalid value for " + option);
    }

    return number;
}

/**
 * @brief Remove the whitespace accepted around dotenv keys and values.
 */
string trimWhitespace(const string& text) {
    const auto first = text.find_first_not_of(" \t\r\n");
    return first == string::npos ? "" : text.substr(first, text.find_last_not_of(" \t\r\n") - first + 1);
}
} // namespace

ParserKind parserKind(const string& name) {
    if(name != "ollama" && name != "regex") {
        throw runtime_error("Parser must be ollama or regex, not \"" + name + "\"");
    }

    return name == "regex" ? ParserKind::Regex : ParserKind::Ollama;
}

Settings parseArguments(int argc, char* argv[]) {
    Settings settings;
    bool modelSet = false;

    for(int i = 1; i < argc; ++i) {
        const string arg = argv[i];
        /*
         * @brief Consume the argument after the current option as its value.
         */
        const auto value = [&] {
            if(++i == argc) {
                throw runtime_error("Missing value for " + arg);
            }

            return string(argv[i]);
        };
        /*
         * @brief Select a mode, rejecting a second one.
         */
        const auto selectMode = [&](Mode mode) {
            if(settings.mode != Mode::Listen) {
                throw runtime_error("Choose only one of --list-devices, --check-phrases, or --parse");
            }

            settings.mode = mode;
        };

        if(arg == "-h" || arg == "--help") {
            settings.mode = Mode::Help;
            return settings;
        }

        if(arg == "--list-devices") {
            selectMode(Mode::ListDevices);
        } else if(arg == "--check-phrases") {
            selectMode(Mode::CheckPhrases);
        } else if(arg == "--parse") {
            selectMode(Mode::Parse);
            settings.parseText = value();
        } else if(arg == "--phrases") {
            settings.phraseFile = value();
        } else if(arg == "--parser") {
            settings.parser = parserKind(value());
        } else if(arg == "--ollama-model") {
            settings.ollamaModel = value();
        } else if(arg == "--home") {
            settings.homeFile = value();
        } else if(arg == "--wake-word") {
            settings.wakeWord = value();
        } else if(arg == "--log-level") {
            settings.logLevel = value();
        } else if(arg == "--log-file") {
            settings.logFile = value();
        } else if(arg == "--cpu") {
            settings.gpu = false;
        } else if(arg == "--threads") {
            settings.threads = toNumber<int>(value(), arg);
        } else if(arg == "--threshold") {
            settings.threshold = toNumber<float>(value(), arg);
        } else if(arg == "--silence-ms") {
            settings.silenceMs = toNumber<int>(value(), arg);
        } else if(arg.empty() || arg[0] != '-') {
            if(modelSet) {
                throw runtime_error("Only one model path is accepted");
            }

            settings.model = arg;
            modelSet = true;
        } else {
            throw runtime_error("Unknown option: " + arg);
        }
    }

    if(settings.threads < 1 || settings.threads > 256 || !std::isfinite(settings.threshold) ||
       settings.threshold <= 0 || settings.threshold >= 1 || settings.silenceMs < 200 ||
       settings.silenceMs > 3000) {
        throw runtime_error("Expected threads 1..256, threshold 0..1, silence-ms 200..3000");
    }

    return settings;
}

ConnectionSettings readConnectionSettings(const string& path) {
    std::ifstream input(path);
    ConnectionSettings settings;
    const std::pair<const char*, string*> keys[] = {{"HOME_ASSISTANT_URL", &settings.url}, {"HOME_ASSISTANT_TOKEN", &settings.token},
                                                    {"OLLAMA_URL", &settings.ollamaUrl}, {"OLLAMA_MODEL", &settings.ollamaModel}, {"PARSER", &settings.parser}};
    string line;

    // The environment is the general fallback; this installation's file overrides it.
    for(const auto& [name, setting] : keys) {
        if(const char* value = std::getenv(name)) {
            *setting = value;
        }
    }

    while(std::getline(input, line)) {
        line = trimWhitespace(line);

        if(line.empty() || line[0] == '#') {
            continue;
        }

        if(line.compare(0, 7, "export ") == 0) {
            line = trimWhitespace(line.substr(7));
        }

        const auto equals = line.find('=');

        if(equals == string::npos) {
            throw runtime_error("Invalid environment-file entry");
        }

        const string key = trimWhitespace(line.substr(0, equals));
        string value = trimWhitespace(line.substr(equals + 1));

        if(value.size() >= 2 && (value.front() == '"' || value.front() == '\'') &&
           value.back() == value.front()) {
            value = value.substr(1, value.size() - 2);
        }

        for(const auto& [name, setting] : keys) {
            if(key == name) {
                *setting = value;
            }
        }
    }

    return settings;
}

void printUsage(const string& executable) {
    spdlog::info("Usage: {} [model.bin] [--threads N] [--threshold RMS] [--silence-ms N] [--cpu] [--parser ollama|regex] [--ollama-model NAME] [--phrases FILE] [--home FILE] "
                 "[--wake-word TEXT] [--log-level LEVEL] [--log-file FILE] [--list-devices | --check-phrases | --parse TEXT] [--help]",
                 executable);
}