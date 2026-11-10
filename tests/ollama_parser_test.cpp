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
    string cutOff;
    string lastPrompt;
    server.Post("/api/chat", [&](const httplib::Request& request, httplib::Response& response) {
        lastPrompt = request.body;
        response.set_content(json{{"message", {{"content", cutOff.empty() ? answer.dump() : cutOff}}}, {"done_reason", cutOff.empty() ? "stop" : "length"}}.dump(), "application/json");
    });
    std::vector<string> prerequests;
    bool prerequestKeepsLoaded = false;
    int prerequestStatus = 200;
    server.Post("/api/generate", [&](const httplib::Request& request, httplib::Response& response) {
        prerequests.push_back(json::parse(request.body).at("prompt").get<string>());
        prerequestKeepsLoaded = json::parse(request.body).value("keep_alive", 0) == -1;
        response.status = prerequestStatus;
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
    check(accepted && accepted->service == "turn_off" && accepted.refusal.empty(), "a plain request is accepted");

    for(const auto* text : {"don't turn off the q800", "Don't turn off the Q800.", "do not turn off the q800", "never turn off the q800"}) {
        const auto refused = parser.parse(text, context);
        check(!refused && !refused.refusal.empty(), string("a negated request is refused: ") + text);
    }

    for(const auto* text : {"turn off the q800 then pause the q990", "turn off the q800 and pause the q990"}) {
        const auto refused = parser.parse(text, context);
        check(!refused && !refused.refusal.empty(), string("a compound request is refused, not half done: ") + text);
    }

    answer = {{"domain", "media_player"}, {"service", "turn_off"}, {"heard", "bed and bath q800"}, {"entity", "media_player.q800"}, {"data", json::object()}, {"reply", ""}};
    check(parser.parse("turn off the bed and bath q800", context).has_value(), "an \"and\" inside a device name is not a compound request");
    answer = request("rock and roll");
    check(parser.parse("play rock and roll", context).has_value(), "an \"and\" inside a song title is not a compound request");
    answer = request("never gonna give you up");
    const auto song = parser.parse("play never gonna give you up", context);
    check(song && song->data["song"] == "never gonna give you up", "a negative word in a song title is not a refusal");
    answer = request("wonderwall");
    check(!parser.parse("don't play wonderwall", context), "a negated song request is refused");
    const string opening = "<|im_start|>system\n";
    check(!prerequests.empty() && prerequests.back().rfind(opening, 0) == 0, "a prerequest goes before the model is asked");
    check(!prerequestKeepsLoaded && !json::parse(lastPrompt).contains("keep_alive"), "unloading is left to the server by default");
    Context remembered = context;
    remembered.defaultDevice = "media_player.q800";
    answer = {{"domain", "media_player"}, {"service", "turn_off"}, {"heard", "it"}, {"entity", ""}, {"data", json::object()}, {"reply", ""}};
    const auto followUp = parser.parse("turn it off", remembered);
    check(followUp && followUp->entity == "media_player.q800", "\"it\" means the remembered device, whatever entity the model gave");
    const auto noMemory = parser.parse("turn it off", context);
    check(!noMemory && !noMemory.refusal.empty(), "\"it\" with nothing remembered is refused with a reason");
    const Device kitchen{"media_player.kitchen", "Kitchen", "media_player", "on"};
    Context talking({Device{"media_player.q800", "Q800B", "media_player", "on"}, kitchen}, services);
    talking.defaultDevice = "media_player.q800";
    talking.history = {{"pause the kitchen", Command{"media_player", "media_pause", "media_player.kitchen", json::object()}, true}, {"turn off the q800", Command{"media_player", "turn_off", "media_player.q800", json::object()}, false}};
    const auto pointing = [](const char* heard, const char* entity) { return json{{"domain", "media_player"}, {"service", "turn_off"}, {"heard", heard}, {"entity", entity}, {"data", json::object()}, {"reply", ""}}; };

    answer = pointing("it", "media_player.q800");
    check(parser.parse("turn it off", talking).has_value() && lastPrompt.find("pause the kitchen") != string::npos && lastPrompt.find("(failed)") != string::npos, "the model is told the conversation and how it went");
    const auto loaded = prerequests.back().substr(opening.size());
    const auto sent = json::parse(lastPrompt).at("messages")[0].at("content").get<string>();
    check(sent.rfind(loaded, 0) == 0 && sent.size() > loaded.size() && loaded.find("Earlier requests") == string::npos, "the prerequest is exactly the start of the prompt, without the conversation");
    prerequestStatus = 500;
    check(parser.parse("turn it off", talking).has_value(), "a failed prerequest doesn't stop the command");
    prerequestStatus = 200;
    OllamaParser direct("http://127.0.0.1:" + std::to_string(port), "test", false);
    const auto before = prerequests.size();
    check(direct.parse("turn it off", talking).has_value() && prerequests.size() == before, "the prerequest can be turned off");
    OllamaParser kept("http://127.0.0.1:" + std::to_string(port), "test", true, true);
    check(kept.parse("turn it off", talking).has_value() && prerequestKeepsLoaded && json::parse(lastPrompt).at("keep_alive") == -1, "both requests can keep the model loaded");
    parser.parse("turn it off", context);
    check(lastPrompt.find("Earlier requests") == string::npos, "with no conversation the prompt says nothing about one");
    answer = pointing("the other one", "media_player.kitchen");
    const auto otherOne = parser.parse("turn off the other one", talking);
    check(otherOne && otherOne->entity == "media_player.kitchen", "\"the other one\" is the latest device that isn't the current one");
    answer = pointing("it", "media_player.kitchen");
    const auto olderPick = parser.parse("turn it off", talking);
    check(olderPick && olderPick->entity == "media_player.q800", "\"it\" is the remembered device even if the model picks an older one");
    Context elsewhere = talking;
    elsewhere.defaultDevice.clear();
    const auto noDefault = parser.parse("turn it off", elsewhere);
    check(noDefault && noDefault->entity == "media_player.q800", "with nothing remembered in the domain, \"it\" is the latest in the conversation");
    answer = pointing("other one back on", "media_player.bedroom");
    const auto wordy = parser.parse("turn off the other one back on", talking);
    check(wordy && wordy->entity == "media_player.kitchen", "extra words in heard don't hide \"the other one\", and the model's device is ignored");
    Context onlyOne = talking;
    onlyOne.history.erase(onlyOne.history.begin());
    answer = pointing("the other one", "media_player.kitchen");
    check(!parser.parse("turn off the other one", onlyOne), "with no other device in the conversation, \"the other one\" is refused");
    answer = pointing("it", "media_player.bedroom");
    const auto invented = parser.parse("turn it off", talking);
    check(invented && invented->entity == "media_player.q800", "a device outside the conversation falls back to the remembered one");
    Context forgetful({Device{"media_player.q800", "Q800B", "media_player", "on"}, kitchen}, services);
    forgetful.defaultDevice = "media_player.q800";
    answer = pointing("", "media_player.kitchen");
    const auto blank = parser.parse("turn off the kitchen", forgetful);
    check(!blank && !blank.refusal.empty(), "a blank heard can't send a named device's command to the remembered one");
    answer = pointing("", "");
    const auto louder = parser.parse("turn off please", forgetful);
    check(louder && louder->entity == "media_player.q800", "a request with no device means the remembered one");
    answer = {{"domain", "media_player"}, {"service", "request_song"}, {"heard", ""}, {"entity", ""}, {"data", {{"song", "wonderwall"}}}, {"reply", ""}};
    const auto misfiled = parser.parse("play wonderwall", context);
    check(misfiled && misfiled->domain == "spotify", "a service under the wrong domain moves to the only one offering it");
    answer = {{"domain", "media_player"}, {"service", "no_such_service"}, {"heard", ""}, {"entity", ""}, {"data", json::object()}, {"reply", ""}};
    check(!parser.parse("do something odd", context), "a service nobody offers isn't a command");
    answer = {{"answer", " Paris, sir. "}};
    const auto question = parser.parse("what is the capital of france", context);
    check(!question && question.answer == "Paris, sir." && question.refusal.empty(), "the model can answer instead of commanding");
    check(json::parse(lastPrompt).at("format").contains("anyOf"), "the reply may be either a command or an answer");
    answer = {{"answer", ""}};
    const auto overheard = parser.parse("so anyway I told her", context);
    check(!overheard && overheard.answer.empty() && overheard.refusal.empty(), "an empty answer stays silent");
    Context chatting = context;
    chatting.history = {{"what is the capital of france", Command{}, true, "Paris, sir."}};
    parser.parse("and of spain", chatting);
    check(lastPrompt.find("you said") != string::npos && lastPrompt.find("Paris, sir.") != string::npos, "the model is told what it said");
    cutOff = R"({"answer": "It was 3.5 metres long. Then it grew. And after that it ne)";
    const auto longAnswer = parser.parse("tell me a story", context);
    check(!longAnswer && longAnswer.answer == "It was 3.5 metres long. Then it grew.", "an answer cut off by the token limit keeps its finished sentences");
    cutOff = R"({"answer": "Once upon a ti)";
    const auto noSentence = parser.parse("tell me a story", context);
    check(!noSentence && noSentence.answer.empty() && !noSentence.refusal.empty(), "an answer cut off before its first sentence ends is a failure");
    cutOff = R"({"domain": "media_player", "service": "turn_of)";
    const auto brokenCommand = parser.parse("turn off the q800", context);
    check(!brokenCommand && brokenCommand.answer.empty() && !brokenCommand.refusal.empty(), "a command cut off by the token limit is a failure");
    cutOff.clear();
    server.stop();
    serving.join();
    std::cout << checks << " Ollama parser checks passed\n";
}