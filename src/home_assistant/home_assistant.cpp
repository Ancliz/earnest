/**
 * @file
 * @brief Home Assistant HTTP transport and structured execution outcomes.
 */

#include "home_assistant.hpp"
#include "control_policy.hpp"

#include <stdexcept>

using Commands::json;
using std::runtime_error;
using std::string;

HomeAssistant::HomeAssistant(const string& url, const string& token) : client(url) {
    if(token.empty()) {
        throw runtime_error("Set HOME_ASSISTANT_TOKEN before starting voice control");
    }

    client.set_default_headers({{"Authorization", string("Bearer ") + token}});
    client.set_connection_timeout(5);
    client.set_read_timeout(10);
    client.set_write_timeout(10);
}

json HomeAssistant::getStates() {
    const auto response = client.Get("/api/states");

    if(!response) {
        throw runtime_error("Cannot query Home Assistant states: " + httplib::to_string(response.error()));
    }

    if(response->status != 200) {
        throw runtime_error("Home Assistant states returned HTTP " + std::to_string(response->status));
    }

    auto states = json::parse(response->body, nullptr, false);

    if(!states.is_array()) {
        throw runtime_error("Home Assistant returned invalid states JSON");
    }

    return states;
}

json HomeAssistant::getServices() {
    const auto response = client.Get("/api/services");

    if(!response || response->status != 200) {
        throw runtime_error("Cannot query Home Assistant services");
    }

    auto services = json::parse(response->body, nullptr, false);

    if(!services.is_array()) {
        throw runtime_error("Invalid Home Assistant services JSON");
    }

    return services;
}

Commands::ExecutionResult HomeAssistant::check(const json& request, const json& configuration) {
    try {
        auto result = prepareCommand(request, getStates(), configuration);

        if(!result.success) {
            return result;
        }

        const string path = result.request.at("endpoint").get<string>().substr(string("/services/").size());
        const auto separator = path.find('/');
        const string domain = path.substr(0, separator);
        const string service = path.substr(separator + 1);

        for(const auto& entry : getServices()) {
            if(entry.is_object() && entry.value("domain", string()) == domain && entry.contains("services") &&
               entry["services"].is_object() && entry["services"].contains(service)) {
                return result;
            }
        }

        return {false, "Service is unavailable: " + path};
    } catch(const std::exception& error) {
        return {false, string("Cannot verify command: ") + error.what()};
    }
}

Commands::ExecutionResult HomeAssistant::execute(const json& request, const json& configuration) {
    auto result = check(request, configuration);

    if(!result.success) {
        return result;
    }

    try {
        const string endpoint = result.request.at("endpoint").get<string>();
        const auto response =
            client.Post("/api" + endpoint, result.request.at("body").dump(), "application/json");

        if(!response) {
            return {false, "Home Assistant request failed: " + httplib::to_string(response.error())};
        }

        if(response->status < 200 || response->status >= 300) {
            return {false, "Home Assistant returned HTTP " + std::to_string(response->status) + ": " +
                               response->body};
        }

        return result;
    } catch(const std::exception& error) {
        return {false, string("Home Assistant request failed: ") + error.what()};
    }
}
