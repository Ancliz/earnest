/**
 * @file
 * @brief Regression suite for the checks OllamaParser applies to a model's answer.
 */

#include "check.hpp"
#include "ollama/ollama_parser.hpp"

#include <iostream>
#include <thread>

using Commands::Device;
using Commands::ServiceCatalogue;
using Commands::ServiceFields;

/**
 * @brief Verify that negated requests are refused whatever the model answers, without catching song titles.
 */
int main() {
    httplib::Server server;
    json answer;
    server.Post("/api/chat", [&](const httplib::Request&, httplib::Response& response) {
        response.set_content(json{{"message", {{"content", answer.dump()}}}}.dump(), "application/json");
    });
    const int port = server.bind_to_any_port("127.0.0.1");

    if(port <= 0) {
        throw std::runtime_error("Cannot bind mock HTTP server");
    }

    std::thread serving([&] { server.listen_after_bind(); });
    server.wait_until_ready();
    OllamaParser parser("http://127.0.0.1:" + std::to_string(port), "test");
    const ServiceCatalogue services = {{"media_player", {{"turn_off", ServiceFields{}}}},
                                       {"spotify", {{"request_song", ServiceFields{{"song"}, {"song"}, {}, false, ""}}}}};
    const Context context({Device{"media_player.q800", "Q800B", "media_player", "on"}}, services);
    const json turnOff = {{"domain", "media_player"}, {"service", "turn_off"}, {"heard", "q800"}, {"entity", "media_player.q800"}, {"data", json::object()}, {"reply", "Off."}};
    const auto request = [](const char* song) { return json{{"domain", "spotify"}, {"service", "request_song"}, {"heard", ""}, {"entity", ""}, {"data", {{"song", song}}}, {"reply", ""}}; };

    answer = turnOff;
    const auto accepted = parser.parse("turn off the q800", context);
    check(accepted && accepted->service == "turn_off" && parser.failureReason().empty(), "a plain request is accepted");

    for(const auto* text : {"don't turn off the q800", "Don't turn off the Q800.", "do not turn off the q800", "never turn off the q800"}) {
        check(!parser.parse(text, context) && !parser.failureReason().empty(), string("a negated request is refused: ") + text);
    }

    answer = request("never gonna give you up");
    const auto song = parser.parse("play never gonna give you up", context);
    check(song && song->data["song"] == "never gonna give you up", "a negative word in a song title is not a refusal");
    answer = request("wonderwall");
    check(!parser.parse("don't play wonderwall", context), "a negated song request is refused");
    server.stop();
    serving.join();
    std::cout << checks << " Ollama parser checks passed\n";
}