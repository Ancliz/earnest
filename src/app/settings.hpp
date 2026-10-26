#pragma once

#include "parser/parser.hpp"
#include <algorithm>
#include <string>
#include <thread>

using std::string;
using std::thread;

struct Settings {
    string model = "models/ggml-base.en.bin";
    int threads = static_cast<int>(std::max(1u, std::min(4u, thread::hardware_concurrency())));
    float threshold = 0.01f;
    int silenceMs = 700;
    bool gpu = true;
    string phrases = Parser::defaultPhraseFile();
    bool listDevices = false;
    bool checkPhrases = false;
    string parseText;
    bool parseOnly = false;
};

Settings parseArguments(int argc, char* argv[]);
void loadEnvironment(const string& path = ".env");
void printUsage(const string& executable);