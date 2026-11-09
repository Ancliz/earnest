/**
 * @file
 * @brief Regression suite for the Spotify executor and executor routing against a local mock server.
 */

#include "check.hpp"
#include "app/executor_router.hpp"
#include "spotify/spotify.hpp"

#include <atomic>
#include <iostream>
#include <thread>

using httplib::Request;
using httplib::Response;
using std::atomic;

namespace {
/**
 * @brief A Spotify track object as the server returns it.
 */
json track(const string& name, const string& artist, const string& uri) {
    return {{"name", name}, {"uri", uri}, {"artists", {{{"name", artist}}}}};
}

/**
 * @brief An executor that owns one domain and records what it was asked to run.
 */
struct FakeExecutor final : Executor {
    /// Domain this executor offers.
    string domain;
    /// Commands executed.
    std::vector<Command> executed;
    /// Whether discovery fails, as for an unreachable server.
    bool unreachable = false;

    /**
     * @brief Offer one domain with a single entity-targeted service.
     */
    explicit FakeExecutor(string domain) : domain(std::move(domain)) {}

    /**
     * @brief Accept any configuration.
     */
    void configure(const json&) override {}

    /**
     * @brief One device in the domain, or a failure when unreachable.
     */
    Devices discoverDevices() override {
        if(unreachable) {
            throw std::runtime_error("unreachable");
        }

        return {{domain + ".one", "One", domain, "on", json::object()}};
    }

    /**
     * @brief A turn_on service in the domain, or a failure when unreachable.
     */
    ServiceCatalogue discoverServices() override {
        if(unreachable) {
            throw std::runtime_error("unreachable");
        }

        return {{domain, {{"turn_on", {}}}}};
    }

    /**
     * @brief Accept every command.
     */
    Result preview(const Command& command) override {
        return {true, "", command};
    }

    /**
     * @brief Record and accept every command.
     */
    Result execute(const Command& command) override {
        executed.push_back(command);
        return {true, "", command, domain};
    }
};
} // namespace

/**
 * @brief Verify Spotify discovery, validation and replies, and routing between executors by domain.
 */
int main() {
    httplib::Server server;
    atomic<int> transfers{0};
    atomic<bool> transferFails{false};
    atomic<int> requests{0};
    json transferred;
    json requested;
    const json queue = {{"currently_playing", track("Song 2", "Blur", "spotify:track:a")},
                        {"queue", {track("Bohemian Rhapsody", "Queen", "spotify:track:b"), track("Yellow", "Coldplay", "spotify:track:c")}}};

    server.Get("/devices", [&](const Request&, Response& response) {
        const json devices = {{{"id", "abc"}, {"name", "Lounge Speaker"}, {"is_active", true}},
                              {{"id", "idle"}, {"name", "Idle Speaker"}, {"is_active", false}},
                              {{"id", "locked"}, {"name", "Locked"}, {"is_restricted", true}}};
        response.set_content(devices.dump(), "application/json");
    });
    server.Get("/queue", [&](const Request&, Response& response) { response.set_content(queue.dump(), "application/json"); });
    // The server matches the spoken song against the queue: 0 ms is playing now, 404 is not queued.
    server.Get("/time-until", [&](const Request& request, Response& response) {
        const auto song = request.get_param_value("song");

        if(song == "bohemian rhapsody") {
            response.set_content(json{{"track", queue["queue"][0]}, {"ms", 200000}}.dump(), "application/json");
        } else if(song == "song 2") {
            response.set_content(json{{"track", queue["currently_playing"]}, {"ms", 0}}.dump(), "application/json");
        } else {
            response.status = 404;
        }
    });
    server.Post("/request", [&](const Request& request, Response& response) {
        ++requests;
        requested = json::parse(request.body);
        response.set_content(json{{"track", track(requested["song"], "Someone", "spotify:track:found")}}.dump(), "application/json");
    });
    server.Put("/transfer", [&](const Request& request, Response& response) {
        ++transfers;
        transferred = json::parse(request.body);

        if(transferFails) {
            response.status = 502;
            return;
        }

        response.status = 204;
    });
    const int port = server.bind_to_any_port("127.0.0.1");

    if(port <= 0) {
        throw std::runtime_error("Cannot bind mock HTTP server");
    }

    std::thread serving([&] { server.listen_after_bind(); });
    server.wait_until_ready();
    bool passed = true;

    try {
        Spotify spotify("http://127.0.0.1:" + std::to_string(port));
        const auto devices = spotify.discoverDevices();
        check(devices.size() == 2 && devices[0].entityId == "spotify.abc" && devices[0].name == "Lounge Speaker" && devices[0].state == "active",
              "Spotify Connect devices are discovered, skipping restricted ones");
        const auto services = spotify.discoverServices();
        check(!findService(services, "spotify", "get_queue")->targetsEntity && findService(services, "spotify", "cast")->targetsEntity,
              "only cast targets a device");

        check(spotify.execute({"spotify", "cast", "spotify.abc", json::object()}).success && transfers == 1 && transferred["device_id"] == "abc",
              "cast transfers playback to the device");
        check(!spotify.execute({"spotify", "cast", "spotify.gone", json::object()}).success && transfers == 1, "cast needs an available device");
        check(spotify.preview({"spotify", "cast", "spotify.abc", json::object()}).success && transfers == 1, "preview sends nothing");
        transferFails = true;
        check(spotify.execute({"spotify", "cast", "spotify.abc", json::object()}).success, "a reported failure is ignored when the device became active");
        check(!spotify.execute({"spotify", "cast", "spotify.idle", json::object()}).success, "a reported failure stands when the device stays idle");
        transferFails = false;

        const auto listed = spotify.execute({"spotify", "get_queue", "", json::object()});
        check(listed.success && listed.reply == "Playing Song 2 by Blur. Next: Bohemian Rhapsody by Queen, Yellow by Coldplay.", "the queue is described");

        const auto queued = spotify.execute({"spotify", "request_song", "", {{"song", "Wonderwall"}}});
        check(queued.success && requests == 1 && requested["song"] == "Wonderwall" && queued.reply == "Queued Wonderwall by Someone.",
              "a requested song is passed to the server and its match reported");
        check(!spotify.execute({"spotify", "request_song", "", json::object()}).success && requests == 1, "a song request needs a song");
        check(!spotify.execute({"spotify", "request_song", "", {{"song", 42}}}).success, "a song must be text");
        check(!spotify.execute({"spotify", "play", "", json::object()}).success, "playback controls are not Spotify's");

        const auto waiting = spotify.execute({"spotify", "time_until", "", {{"song", "bohemian rhapsody"}}});
        check(waiting.success && waiting.reply == "Bohemian Rhapsody by Queen plays in 3 minutes 20 seconds.", "time until a queued song");
        const auto current = spotify.execute({"spotify", "time_until", "", {{"song", "song 2"}}});
        check(current.success && current.reply == "Song 2 by Blur is playing now.", "the current song is playing now");
        const auto absent = spotify.execute({"spotify", "time_until", "", {{"song", "Africa"}}});
        check(absent.success && absent.reply == "\"Africa\" is not in the queue.", "a song not in the queue says so");

        // Routing: the first executor offering a domain owns it, and an unreachable one is skipped.
        FakeExecutor first("media_player");
        FakeExecutor clash("media_player");
        FakeExecutor down("light");
        down.unreachable = true;
        ExecutorRouter router({&first, &clash, &down, &spotify});
        const auto catalogue = router.discoverServices();
        check(catalogue.count("media_player") && catalogue.count("spotify") && !catalogue.count("light"), "services merge across executors");
        check(router.discoverDevices().size() == 4, "devices merge, skipping unreachable executors");
        check(router.execute({"media_player", "turn_on", "media_player.one", json::object()}).reply == "media_player" && first.executed.size() == 1 &&
                  clash.executed.empty(),
              "the first executor offering a domain runs its commands");
        check(router.execute({"spotify", "get_queue", "", json::object()}).success, "spotify commands reach the Spotify executor");
        check(!router.execute({"light", "turn_on", "light.one", json::object()}).success, "an unowned domain is rejected");
    } catch(const std::exception& error) {
        std::cerr << error.what() << '\n';
        passed = false;
    }

    server.stop();
    serving.join();

    if(!passed) {
        throw std::runtime_error("Spotify executor test failed");
    }

    std::cout << checks << " Spotify and routing checks passed\n";
}