#include "settings.hpp"

#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <stdexcept>

using std::runtime_error;
using std::size_t;

Settings parseArguments(int argc, char* argv[]) {
    Settings settings;
    bool modelSet = false;

    for(int i = 1; i < argc; ++i) {
        const string arg = argv[i];

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

void loadEnvironment(const string& path) {
    std::ifstream input(path);
    string line;

    while(std::getline(input, line)) {
        const auto equals = line.find('=');

        if(line.empty() || line[0] == '#' || equals == string::npos) {
            continue;
        }

        string key = line.substr(0, equals);
        string value = line.substr(equals + 1);

        if(key.compare(0, 7, "export ") == 0) {
            key.erase(0, 7);
        }

        if(value.size() >= 2 && ((value.front() == '\"' && value.back() == '\"') ||
                                 (value.front() == '\'' && value.back() == '\''))) {
            value = value.substr(1, value.size() - 2);
        }

        if(!std::getenv(key.c_str())) {
            setenv(key.c_str(), value.c_str(), 0);
        }
    }
}

void printUsage(const string& executable) {
    std::cout
        << "Usage: " << executable
        << " [model.bin] [--threads N] [--threshold RMS] "
           "[--silence-ms N] [--cpu] [--phrases FILE] [--list-devices] [--check-phrases] [--parse TEXT]\n";
}