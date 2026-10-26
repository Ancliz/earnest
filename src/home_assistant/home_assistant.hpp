#pragma once

#include "device_aliases.hpp"
#include <httplib.h>
#include <string>

class HomeAssistant {
    httplib::Client client;

public:
    explicit HomeAssistant(const std::string& url, const char* token);
    Parser::json getStates();
    Parser::json getServices();
    bool post(const Parser::json& request, const Parser::json& configuration);
};