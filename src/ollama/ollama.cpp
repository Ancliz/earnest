/**
 * @file
 * @brief Ollama chat requests.
 */

#include "ollama.hpp"

#include <chrono>
#include <spdlog/spdlog.h>

using Clock = std::chrono::steady_clock;
using nlohmann::json;
using nlohmann::ordered_json;
using std::runtime_error;
using std::string;

namespace {
/**
 * @brief Log how long a request took, and the breakdown Ollama reports when the reply has one.
 * @param[in] request What was asked for, to tell the log lines apart.
 * @param[in] start When the request was sent.
 * @param[in] reply Decoded response; anything but an object just has no breakdown.
 */
void logTiming(const char* request, Clock::time_point start, const json& reply) {
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start).count();
    const auto report = reply.is_object() ? reply : json::object();
    const auto milliseconds = [&](const char* field) { return report.value(field, 0LL) / 1000000; };
    spdlog::info("Ollama {} took {} ms: load {} ms, prompt {} ms ({} tokens), generating {} ms ({} tokens)", request, elapsed, milliseconds("load_duration"),
                 milliseconds("prompt_eval_duration"), report.value("prompt_eval_count", 0), milliseconds("eval_duration"), report.value("eval_count", 0));
}
} // namespace

OllamaClient::OllamaClient(const string& url, const string& model) : client(url.c_str()), model(model) {
    client.set_connection_timeout(5);
    client.set_read_timeout(30);
    client.set_write_timeout(30);
}

json OllamaClient::chat(const json& messages, const json& tools, const ordered_json& format) {
    // Thinking models otherwise reason at length before answering; a command needs neither
    // reasoning nor randomness, and a capped reply cannot run past the read timeout.
    ordered_json request = {{"model", model},
                            {"stream", false},
                            {"think", false},
                            {"options", {{"temperature", 0}, {"num_predict", 256}}},
                            {"messages", ordered_json::parse(messages.dump())}};

    if(!format.empty()) {
        request["format"] = format;
    }

    if(!tools.empty()) {
        request["tools"] = ordered_json::parse(tools.dump());
    }

    spdlog::debug("Ollama request:\n{}", request.dump(2));
    const auto start = Clock::now();
    const auto response = client.Post("/api/chat", request.dump(), "application/json");

    if(!response) {
        throw runtime_error("Cannot query Ollama: " + httplib::to_string(response.error()));
    }

    const auto reply = json::parse(response->body, nullptr, false);
    spdlog::debug("Ollama response (HTTP {}):\n{}", response->status, reply.is_discarded() ? response->body : reply.dump(2));

    if(response->status != 200) {
        throw runtime_error("Ollama returned HTTP " + std::to_string(response->status));
    } else if(reply.is_discarded()) {
        throw runtime_error("Ollama returned invalid JSON");
    }

    logTiming("chat", start, reply);
    return reply;
}

void OllamaClient::sendPrerequest(const string& systemStart) {
    // One token is the least a generate request can ask for, and its answer is of no use.
    const ordered_json request = {{"model", model},
                                  {"stream", false},
                                  {"raw", true},
                                  {"prompt", "<|im_start|>system\n" + systemStart},
                                  {"options", {{"temperature", 0}, {"num_predict", 1}}}};
    const auto start = Clock::now();
    const auto response = client.Post("/api/generate", request.dump(), "application/json");

    if(!response) {
        throw runtime_error("Cannot query Ollama: " + httplib::to_string(response.error()));
    } else if(response->status != 200) {
        spdlog::warn("Ollama prerequest returned HTTP {}; continuing without it", response->status);
    } else {
        logTiming("prerequest", start, json::parse(response->body, nullptr, false));
    }
}