#include "home_assistant.hpp"
#include "control_policy.hpp"

#include <iostream>
#include <stdexcept>

using Parser::json;
using std::runtime_error;
using std::string;

HomeAssistant::HomeAssistant(const string& url, const char* token) : client(url) {
    if(!token || !*token) {
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

bool HomeAssistant::post(const json& request, const json& configuration) {
    string problem;

    try {
        problem = validateCommand(request, getStates(), configuration);
    } catch(const std::exception& error) {
        std::cerr << "Cannot verify live device state: " << error.what() << '\n';
        return false;
    }

    if(!problem.empty()) {
        std::cerr << "Command rejected: " << problem << '\n';
        return false;
    }

    const string endpoint = request["endpoint"].get<string>();
    const auto& body = request["body"];

    if(endpoint != "/conversation/process") {
        const string path = endpoint.substr(string("/services/").size());
        const auto separator = path.find('/');
        const string domain = path.substr(0, separator);
        const string service = path.substr(separator + 1);
        bool available = false;

        try {
            for(const auto& entry : getServices()) {
                if(entry.value("domain", string()) == domain && entry.at("services").contains(service)) {
                    available = true;
                }
            }
        } catch(const std::exception& error) {
            std::cerr << "Cannot verify service availability: " << error.what() << '\n';
            return false;
        }

        if(!available) {
            std::cerr << "Service is unavailable: " << path << '\n';
            return false;
        }
    }

    const auto response = client.Post("/api" + endpoint, body.dump(), "application/json");

    if(!response) {
        std::cerr << "Home Assistant request failed: " << httplib::to_string(response.error()) << '\n';
        return false;
    }

    if(response->status < 200 || response->status >= 300) {
        std::cerr << response->status << ": " << response->body << '\n';
        return false;
    }

    if(endpoint == "/conversation/process") {
        const auto result = json::parse(response->body, nullptr, false);

        if(result.is_discarded() || !result.is_object() || !result.contains("response") ||
           !result["response"].is_object() || !result["response"].contains("response_type") ||
           !result["response"]["response_type"].is_string()) {
            std::cerr << "Home Assistant returned an invalid conversation response\n";
            return false;
        }

        if(result["response"].value("response_type", string()) == "error") {
            std::cerr << "Home Assistant could not handle the command: " << result["response"] << '\n';
            return false;
        }
    }

    std::cout << "Command sent\n";
    return true;
}