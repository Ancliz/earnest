#include "home_assistant/home_assistant.hpp"
#include "home_assistant/control_policy.hpp"

#include <atomic>
#include <iostream>
#include <stdexcept>
#include <thread>

using Parser::json;
using std::string;

int main() {
    Parser::Engine phrases;
    json states = {{{"entity_id", "media_player.kitchen"},
                    {"state", "playing"},
                    {"attributes", {{"friendly_name", "Kitchen"}}}},
                   {{"entity_id", "climate.ac"},
                    {"state", "heat"},
                    {"attributes", {{"supported_features", 408}, {"preset_mode", "MyTemp"}}}},
                   {{"entity_id", "climate.ac_tv_rump"},
                    {"state", "heat_cool"},
                    {"attributes", {{"supported_features", 385}, {"friendly_name", "AC TV & Rump"}}}}};
    httplib::Server server;
    std::atomic<int> posts{0};
    std::atomic<bool> authorized{false};
    std::atomic<bool> correctBody{false};
    std::atomic<int> mode{0};
    server.Get("/api/states", [&](const httplib::Request& request, httplib::Response& response) {
        authorized = request.get_header_value("Authorization") == "Bearer test-token";
        response.set_content(states.dump(), "application/json");
    });
    server.Get("/api/services", [&](const httplib::Request&, httplib::Response& response) {
        response.set_content(R"([{"domain":"media_player","services":{"volume_set":{}}}])",
                             "application/json");
    });
    server.Post("/api/services/media_player/volume_set",
                [&](const httplib::Request& request, httplib::Response& response) {
                    ++posts;
                    correctBody = request.get_header_value("Content-Type") == "application/json" &&
                                  json::parse(request.body) ==
                                      json({{"entity_id", "media_player.kitchen"}, {"volume_level", 0.4}});
                    response.set_content("[]", "application/json");
                });
    server.Post("/api/conversation/process", [&](const httplib::Request&, httplib::Response& response) {
        ++posts;
        response.set_content(mode == 1 ? R"({"response":{"response_type":"error"}})"
                                       : R"({"response":{"response_type":"action_done"}})",
                             "application/json");
    });
    const int port = server.bind_to_any_port("127.0.0.1");

    if(port <= 0) {
        throw std::runtime_error("Cannot bind mock HTTP server");
    }

    std::thread serving([&] { server.listen_after_bind(); });
    server.wait_until_ready();
    bool passed = true;

    try {
        HomeAssistant client("http://127.0.0.1:" + std::to_string(port), "test-token");
        Parser::Options options;
        populateDeviceAliases(client.getStates(), options);
        passed =
            authorized &&
            client.post(phrases.parse("set kitchen volume to forty", options), phrases.configuration()) &&
            correctBody && posts == 1;
        const auto unsafe = phrases.parse("turn off tv and rump", options);
        passed = passed && !unsafe.is_null() && !client.post(unsafe, phrases.configuration()) && posts == 1;
        const auto noTarget = Parser::parse("pause");
        passed = passed && !client.post(noTarget, phrases.configuration()) && posts == 1;
        const auto missingService = phrases.parse("pause kitchen", options);
        passed = passed && !client.post(missingService, phrases.configuration()) && posts == 1;
        const auto conversation = phrases.parse("turn kitchen up by ten", options);
        passed = passed && client.post(conversation, phrases.configuration()) && posts == 2;
        mode = 1;
        passed = passed && !client.post(conversation, phrases.configuration()) && posts == 3;
    } catch(const std::exception& error) {
        std::cerr << error.what() << '\n';
        passed = false;
    }

    server.stop();
    serving.join();

    if(!passed) {
        throw std::runtime_error("Home Assistant HTTP and command guard test failed");
    }

    std::cout << "HTTP discovery, authorization, request formatting, and pre-post rejection checks passed\n";
}
