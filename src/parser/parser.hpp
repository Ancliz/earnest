#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <nlohmann/json.hpp>

namespace Parser {

using std::string;
using std::unordered_map;
using json = nlohmann::json;

string normalizeDeviceName(string name);
string defaultPhraseFile();

struct Options {
    unordered_map<string, string> devices;
    unordered_map<string, unordered_map<string, string>> domainDevices;
    unordered_map<string, json> deviceStates;
    bool fuzzyDevices = true;
    bool strictDevices = false;
    string defaultDevice;
    string conversationAgent;
};

json resolveEntityId(const string& name, const string& domain, const Options& options);

class Engine {
    struct Impl;
    std::unique_ptr<Impl> impl;

public:
    explicit Engine(const string& path = defaultPhraseFile());
    ~Engine();
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    // Replaces the active rules only after the entire file has validated and compiled.
    void reload();
    bool reloadIfChanged();
    void addPhrase(const string& ruleId, const string& phrase);
    json parse(const string& input, const Options& options = {}) const;
    const json& configuration() const;
    const string& reloadError() const;
};

// Convenience API using the default YAML file; no HTTP or device control occurs here.
json parse(const string& input, const Options& options = {});
} // namespace Parser