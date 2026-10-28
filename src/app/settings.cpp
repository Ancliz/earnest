/**
 * @file
 * @brief Argument validation, dotenv-style connection lookup, and usage output.
 */

#include "settings.hpp"

#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <stdexcept>

using std::runtime_error;
using std::size_t;
using std::string;

Settings parseArguments(int argc, char* argv[]) {
    Settings settings;
    bool modelSet = false;
    bool modeSet = false;

    for(int i = 1; i < argc; ++i) {
        const string arg = argv[i];

        if(arg == "--list-devices" || arg == "--check-phrases" || arg == "--parse") {
            if(modeSet) {
                throw runtime_error("Choose only one of --list-devices, --check-phrases, or --parse");
            }

            modeSet = true;
        }

        if(arg == "--list-devices" || arg == "--check-phrases") {
            settings.listDevices = arg == "--list-devices";
            settings.checkPhrases = arg == "--check-phrases";
            continue;
        }

        if(arg == "--phrases" || arg == "--parse") {
            if(++i == argc) {
                throw runtime_error("Missing value for " + arg);
            }

            if(arg == "--phrases") {
                settings.phrases = argv[i];
            } else {
                settings.parseText = argv[i];
                settings.parseOnly = true;
            }

            continue;
        }

        if(arg == "--cpu") {
            settings.gpu = false;
            continue;
        }

        if(arg.empty() || arg[0] != '-') {
            if(modelSet) {
                throw runtime_error("Only one model path is accepted");
            }

            settings.model = arg;
            modelSet = true;
            continue;
        }

        if(arg != "--threads" && arg != "--threshold" && arg != "--silence-ms") {
            throw runtime_error("Unknown option: " + arg);
        }

        if(++i == argc) {
            throw runtime_error("Missing value for " + arg);
        }

        const string value = argv[i];
        size_t consumed = 0;

        if(arg == "--threads") {
            settings.threads = std::stoi(value, &consumed);
        } else if(arg == "--threshold") {
            settings.threshold = std::stof(value, &consumed);
        } else {
            settings.silenceMs = std::stoi(value, &consumed);
        }

        if(consumed != value.size()) {
            throw runtime_error("Invalid value for " + arg);
        }
    }

    if(settings.threads < 1 || settings.threads > 256 || !std::isfinite(settings.threshold) ||
       settings.threshold <= 0 || settings.threshold >= 1 || settings.silenceMs < 200 ||
       settings.silenceMs > 3000) {
        throw runtime_error("Expected threads 1..256, threshold 0..1, silence-ms 200..3000");
    }

    return settings;
}

/**
 * @copydoc readHomeAssistantSettings(const std::string&)
 */
HomeAssistantSettings readHomeAssistantSettings(const string& path) {
    std::ifstream input(path);
    HomeAssistantSettings settings;
    string line;
    /*
     * @brief Trim the whitespace accepted around dotenv keys and values.
     *
     * @param[in] value Source text to inspect without modification.
     * @return Copy without leading/trailing spaces, tabs, CR, or LF; empty for whitespace.
     */
    const auto trim = [](const string& value) {
        const auto first = value.find_first_not_of(" \t\r\n");
        return first == string::npos ? string()
                                     : value.substr(first, value.find_last_not_of(" \t\r\n") - first + 1);
    };

    while(std::getline(input, line)) {
        line = trim(line);

        if(line.empty() || line[0] == '#') {
            continue;
        }

        if(line.compare(0, 7, "export ") == 0) {
            line = trim(line.substr(7));
        }

        const auto equals = line.find('=');

        if(equals == string::npos) {
            throw runtime_error("Invalid environment-file entry");
        }

        const string key = trim(line.substr(0, equals));
        string value = trim(line.substr(equals + 1));

        if(value.size() >= 2 && ((value.front() == '\"' && value.back() == '\"') ||
                                 (value.front() == '\'' && value.back() == '\''))) {
            value = value.substr(1, value.size() - 2);
        }

        if(key == "HOME_ASSISTANT_URL") {
            settings.url = value;
        } else if(key == "HOME_ASSISTANT_TOKEN") {
            settings.token = value;
        }
    }

    if(const char* value = std::getenv("HOME_ASSISTANT_URL")) {
        settings.url = value;
    }

    if(const char* value = std::getenv("HOME_ASSISTANT_TOKEN")) {
        settings.token = value;
    }

    return settings;
}

/**
 * @copydoc printUsage(const std::string&)
 */
void printUsage(const string& executable) {
    std::cout
        << "Usage: " << executable
        << " [model.bin] [--threads N] [--threshold RMS] "
           "[--silence-ms N] [--cpu] [--phrases FILE] [--list-devices] [--check-phrases] [--parse TEXT]\n";
}