/**
 * @file
 * @brief Regression suite for home assistant behavior.
 */

#include "parser/parser.hpp"
#include "config/configuration.hpp"
#include "home_assistant/home_assistant.hpp"
#include "home_assistant/device_aliases.hpp"
#include "home_assistant/control_policy.hpp"

#include <atomic>
#include <cmath>
#include <mutex>
#include <iostream>
#include <stdexcept>
#include <thread>

using Commands::json;
using std::string;

/**
 * @brief Test executor effects and validation against a local mock Home Assistant server.
 *
 * Verifies authenticated discovery, read-only previews, one-POST execution, service
 * availability checks, constant-zone protection, conversation-bypass rejection,
 * fresh-state relative volume, HTTP failures, and absence of automatic retries.
 * Stops and joins the server before reporting a caught test failure.
 *
 * @return Zero after all checks succeed and the local server is stopped.
 * @throws std::runtime_error If the mock server cannot bind or any check fails.
 * @note Opens a loopback HTTP listener and worker thread; all control POSTs target the
 *       mock server, never a real Home Assistant installation.
 */
int main() {
    const auto configuration = Config::readYamlFile(Config::defaultFile());
    Parser::RegexParser parser(configuration);
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
     * @brief Obtain a synchronized copy of the most recent mock service request body.
     *
     * @return Copy of the received JSON body, or null before the first captured request.
     * @note Locks the fixture mutex to avoid racing the HTTP worker's writes.
     */
    const auto lastReceived = [&] {
        std::lock_guard<std::mutex> lock(receivedMutex);
        return received;
    };
    /*
     * @brief Serve authenticated fixture state with a selectable current volume.
     *
     * @param[in] request Mock HTTP request whose bearer header must match the test token.
     * @param[out] response Receives HTTP 401 or the synthetic JSON state array.
     * @note Reads the atomic test mode; changes only the response and a local state copy.
     */
    server.Get("/api/states", [&](const httplib::Request& request, httplib::Response& response) {
        if(request.get_header_value("Authorization") != "Bearer test-token") {
            response.status = 401;
            return;
        }

        auto live = states;
        live[0]["attributes"]["volume_level"] = mode == 1 ? 0.7 : 0.4;
        response.set_content(live.dump(), "application/json");
    });
    /*
     * @brief Advertise only the mock media-player volume_set service.
     *
     * The incoming request is intentionally unused.
     *
     * @param[out] response Receives a JSON array advertising the single supported service.
     * @note Omitting other services enables the preview/execution availability regressions.
     */
    server.Get("/api/services", [&](const httplib::Request&, httplib::Response& response) {
        response.set_content(R"([{"domain":"media_player","services":{"volume_set":{}}}])",
                             "application/json");
    });
    /*
     * @brief Capture an authenticated, correctly typed mock control request.
     *
     * @param[in] request Incoming service call with bearer and JSON content-type headers.
     * @param[out] response Receives HTTP 400 for invalid headers, simulated HTTP 500,
     *                      or a successful empty-array response according to fixture mode.
     * @note Accepted requests increment the atomic POST count and replace the stored body
     *       under a mutex. No physical device is controlled.
     */
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
    /*
     * @brief Count any unexpected conversation delegation reaching the mock server.
     *
     * The incoming request is intentionally unused.
     *
     * @param[out] response Receives an empty JSON response for the mock endpoint.
     * @note Increments the POST counter so a policy-bypass regression fails the test.
     */
    server.Post("/api/conversation/process", [&](const httplib::Request&, httplib::Response& response) {
        ++posts;
        response.set_content("{}", "application/json");
    });
    const int port = server.bind_to_any_port("127.0.0.1");

    if(port <= 0) {
        throw std::runtime_error("Cannot bind mock HTTP server");
    }

    /*
     * @brief Run the already-bound local HTTP listener on the fixture's worker thread.
     *
     * The callback serves requests until the test calls server.stop(); the owning
     * scope joins the thread after success or a caught assertion failure.
     */
    std::thread serving([&] { server.listen_after_bind(); });
    server.wait_until_ready();
    /*
     * @brief Raise a descriptive failure without bypassing mock-server cleanup.
     *
     * @param[in] passed Whether the observed HTTP/execution behavior matches expectations.
     * @param[in] description Failure description carried by the exception.
     * @throws std::runtime_error If passed is false; the enclosing test catches it and
     *                            stops/joins the server before failing the suite.
     */
    const auto check = [](bool passed, const string& description) {
        if(!passed) {
            throw std::runtime_error(description);
        }
    };
    bool passed = true;

    try {
        HomeAssistant client("http://127.0.0.1:" + std::to_string(port), "test-token");
        Commands::Executor& executor = client;
        const auto context = buildCommandContext(client.getStates());
        const auto command = parser.parse("set kitchen volume to forty", context);
        check(executor.check(command, configuration).success && posts == 0, "preview sends no POST");
        check(executor.execute(command, configuration).success && posts == 1, "execute sends one POST");
        check(lastReceived() == json({{"entity_id", "media_player.kitchen"}, {"volume_level", 0.4}}),
              "service body is preserved");
        const auto unsafe = parser.parse("turn off tv and rump", context);
        check(!unsafe.is_null() && !executor.execute(unsafe, configuration).success && posts == 1,
              "constant-zone checks apply before execution");
        check(!executor.execute(parser.parse("pause"), configuration).success && posts == 1,
              "unresolved targets never execute");
        const auto missingService = parser.parse("pause kitchen", context);
        check(!executor.check(missingService, configuration).success && posts == 1,
              "preview checks service availability too");
        check(!executor.execute(missingService, configuration).success && posts == 1,
              "missing services never execute");
        const json untrusted = {{"endpoint", "/conversation/process"},
                                {"body", {{"text", "turn off every AC zone"}}}};
        check(!executor.execute(untrusted, configuration).success && posts == 1,
              "a replacement parser cannot bypass policy through conversation");
        const auto relative = parser.parse("turn kitchen up by ten", context);
        mode = 1;
        check(executor.check(relative, configuration).success && posts == 1,
              "relative preview sends no POST");
        check(executor.execute(relative, configuration).success && posts == 2,
              "relative volume executes a direct service");
        check(std::abs(lastReceived()["volume_level"].get<double>() - 0.8) < 1e-6,
              "relative volume uses fresh state rather than the parsing snapshot");
        mode = 2;
        const auto failure = executor.execute(command, configuration);
        check(!failure.success && !failure.error.empty() && posts == 3,
              "HTTP failures return details and are not retried");
        HomeAssistant unauthorized("http://127.0.0.1:" + std::to_string(port), "wrong-token");
        check(!unauthorized.execute(command, configuration).success && posts == 3,
              "failed state discovery does not send commands");
    } catch(const std::exception& error) {
        std::cerr << error.what() << '\n';
        passed = false;
    }

    server.stop();
    serving.join();

    if(!passed) {
        throw std::runtime_error("Home Assistant execution boundary test failed");
    }

    std::cout << "Preview, live-state resolution, execution guards, and HTTP failure checks passed\n";
}
