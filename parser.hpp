#pragma once

#include <string>
#include <unordered_map>
#include <nlohmann/json.hpp>

namespace Parser {

    using std::string;
    using std::unordered_map;
    using json = nlohmann::json;

    // Shared by parsing and alias discovery so names normalize identically.
    string normalizeDeviceName(string name);

    struct Options {
        // Optional spoken name -> entity ID mapping. Empty accepts any device name
        // An alias with an empty entity ID is blocked (e.g. duplicate friendly names)
        unordered_map<string, string> devices;
        bool fuzzyDevices = true;
        string defaultDevice;
        // Empty uses Home Assistant's default conversation agent.
        string conversationAgent;
    };

    // Returns null for unsupported, invalid, or ambiguous commands.
    // Returns {endpoint, body}; endpoint is relative to the Home Assistant /api base.
    // Configure device aliases to return actual Home Assistant entity IDs.
    // An omitted device uses defaultDevice, or a null body.entity_id if none is set.
    // Explicit relative amounts return a /conversation/process request with the original text.
    json parse(const string& input, const Options& options = {});
}
