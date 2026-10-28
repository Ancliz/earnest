/**
 * @file
 * @brief Regression suite for the Home Assistant executor against a local mock server.
 */

#include "check.hpp"
#include "config/configuration.hpp"
#include "home_assistant/home_assistant.hpp"
#include "parser/regex_parser.hpp"

#include <atomic>
#include <cmath>
#include <iostream>
#include <mutex>
#include <thread>

using Commands::Command;
using Commands::Context;
using Commands::json;
using std::string;

/**
 * @brief Verify discovery, read-only previews, single-POST execution, and execution guards.
 *
 * Every control POST targets a loopback mock server, never a real installation.
 */
int main() {
    const auto configuration = Config::readYamlFile(Config::defaultPhraseFile);
    RegexParser parser(configuration);
    const json states = {{{"entity_id", "media_player.kitchen"},
                          {"state", "playing"},
                          {"attributes", {{"friendly_name", "Kitchen"}, {"volume_level", 0.4}}}},
                         {{"entity_id", "climate.ac"},
                          {"state", "heat"},
                          {"attributes", {{"supported_features", 408}, {"preset_mode", "MyTemp"}}}},
                         {{"entity_id", "climate.ac_tv_rump"},
                          {"state", "heat_cool"},
                          {"attributes", {{"supported_features", 385}, {"friendly_name", "AC TV & Rump"}}}}};
    httplib::Server server;
    std::atomic<int> posts{0};
    std::atomic<int> mode{0};
    json received;
    std::mutex receivedMutex;
    /*
     * @brief Copy of the latest service body the mock server accepted.
     */
    const auto lastReceived = [&] {
        std::lock_guard<std::mutex> lock(receivedMutex);
        return received;
    };
    // Mode 1 reports a louder speaker, so relative volume must use fresh state.
    server.Get("/api/states", [&](const httplib::Request& request, httplib::Response& response) {
        if(request.get_header_value("Authorization") != "Bearer test-token") {
            response.status = 401;
            return;
        }

        auto live = states;
        live[0]["attributes"]["volume_level"] = mode == 1 ? 0.7 : 0.4;
        response.set_content(live.dump(), "application/json");
    });
    // Only volume_set is advertised, so other services fail the availability check.
    server.Get("/api/services", [&](const httplib::Request&, httplib::Response& response) {
        response.set_content(R"([{"domain":"media_player","services":{"volume_set":{}}}])",
                             "application/json");
    });
    // Mode 2 simulates a server error after the request is received.
    server.Post("/api/services/media_player/volume_set",
                [&](const httplib::Request& request, httplib::Response& response) {
                    if(request.get_header_value("Authorization") != "Bearer test-token" ||
                       request.get_header_value("Content-Type") != "application/json") {
                        response.status = 400;
                        return;
                    }

                    ++posts;

                    {
                        std::lock_guard<std::mutex> lock(receivedMutex);
                        received = json::parse(request.body);
                    }

                    response.status = mode == 2 ? 500 : 200;
                    response.set_content("[]", "application/json");
                });
    // Any other POST is unexpected and must fail the POST-count checks.
    server.Post(".*", [&](const httplib::Request&, httplib::Response& response) {
        ++posts;
        response.set_content("{}", "application/json");
    });
    const int port = server.bind_to_any_port("127.0.0.1");

    if(port <= 0) {
        throw std::runtime_error("Cannot bind mock HTTP server");
    }

    std::thread serving([&] { server.listen_after_bind(); });
    server.wait_until_ready();
    const string url = "http://127.0.0.1:" + std::to_string(port);
    bool passed = true;

    try {
        HomeAssistant client(url, "test-token");
        client.configure(configuration);
        Commands::Executor& executor = client;
        const Context context{executor.discoverDevices()};
        check(context.devices.size() == 3, "discovery returns every controllable entity");
        const auto command = parser.parse("set kitchen volume to forty", context);
        check(command && executor.preview(*command).success && posts == 0, "preview sends no POST");
        check(executor.execute(*command).success && posts == 1, "execute sends one POST");
        check(lastReceived() == json({{"entity_id", "media_player.kitchen"}, {"volume_level", 0.4}}),
              "service body is preserved");
        const auto unsafe = parser.parse("turn off tv and rump", context);
        check(unsafe && !executor.execute(*unsafe).success && posts == 1,
              "constant-zone checks apply before execution");
        const auto unnamed = parser.parse("pause", {});
        check(unnamed && !executor.execute(*unnamed).success && posts == 1,
              "unresolved targets never execute");
        const auto missingService = parser.parse("pause kitchen", context);
        check(missingService && !executor.preview(*missingService).success && posts == 1,
              "preview checks service availability too");
        check(!executor.execute(*missingService).success && posts == 1, "missing services never execute");
        const Command conversation{"conversation", "process", "", {{"text", "turn off every AC zone"}}};
        check(!executor.execute(conversation).success && posts == 1,
              "a replacement parser cannot bypass policy through conversation");
        const auto relative = parser.parse("turn kitchen up by ten", context);
        mode = 1;
        check(relative && executor.preview(*relative).success && posts == 1,
              "relative preview sends no POST");
        check(executor.execute(*relative).success && posts == 2, "relative volume executes a direct service");
        check(std::abs(lastReceived()["volume_level"].get<double>() - 0.8) < 1e-6,
              "relative volume uses fresh state rather than the parsing snapshot");
        mode = 2;
        const auto failure = executor.execute(*command);
        check(!failure.success && !failure.error.empty() && posts == 3,
              "HTTP failures return details and are not retried");
        HomeAssistant unauthorized(url, "wrong-token");
        unauthorized.configure(configuration);
        check(!unauthorized.execute(*command).success && posts == 3,
              "failed discovery does not send commands");
    } catch(const std::exception& error) {
        std::cerr << error.what() << '\n';
        passed = false;
    }

    server.stop();
    serving.join();

    if(!passed) {
        throw std::runtime_error("Home Assistant execution test failed");
    }

    std::cout << checks << " Home Assistant executor checks passed\n";
}