#pragma once

#include "parser/parser.hpp"
#include <vector>

struct Device {
    std::string entityId;
    std::string friendlyName;
    std::string domain;
    std::string state;
    Parser::json attributes;
};

// Controllable entities, rather than physical-device registry entries.
std::vector<Device> findAvailableDevices(const Parser::json& states, bool includeUnavailable = false);
void populateDeviceAliases(const Parser::json& states, Parser::Options& options);