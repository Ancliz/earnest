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

    if(!reply.is_object() || !reply.contains("total_duration")) {
        spdlog::info("Ollama {} ended after {} ms", request, elapsed);
        return;
    }

    const auto& report = reply;
    const auto milliseconds = [&](const char* field) { return report.value(field, 0LL) / 1000000; };
    spdlog::info("Ollama {} took {} ms: load {} ms, prompt {} ms ({} tokens), generating {} ms ({} tokens)", request, elapsed, milliseconds("load_duration"),
                 milliseconds("prompt_eval_duration"), report.value("prompt_eval_count", 0), milliseconds("eval_duration"), report.value("eval_count", 0));
}
} // namespace

OllamaClient::OllamaClient(const string& url, const string& model, bool keepLoaded) : client(url.c_str()), model(model), keepLoaded(keepLoaded) {
    client.set_connection_timeout(5);
    client.set_read_timeout(30);
    client.set_write_timeout(30);
}

json OllamaClient::chat(const json& messages, const ordered_json& format, double temperature, const TextReceiver& receiver) {
    // Thinking models otherwise reason at length before answering; a command needs no reasoning, and a cap stops a runaway reply.
    ordered_json request = {{"model", model},
                            {"stream", true},
                            {"think", false},
                            {"options", {{"temperature", temperature}, {"num_predict", 256}}},
                            {"messages", ordered_json::parse(messages.dump())},
                            {"format", format}};

    if(keepLoaded) {
        request["keep_alive"] = -1;
    }

    spdlog::debug("Ollama request:\n{}", request.dump(2));
    string partial;
    string text;
    json report;
    bool stopped = false;

    // Each line is one JSON object carrying the next piece of text; the last carries the timings.
    const auto readLine = [&](const string& line) {
        const auto parsed = json::parse(line, nullptr, false);

        if(!parsed.is_object()) {
            return true;
        }

        report = parsed;
        const auto piece = parsed.value("message", json::object()).value("content", string());
        text += piece;
        stopped = !piece.empty() && !receiver(piece);
        return !stopped;
    };
    const auto receive = [&](const char* data, size_t length) {
        partial.append(data, length);

        for(auto end = partial.find('\n'); end != string::npos; end = partial.find('\n')) {
            const auto line = partial.substr(0, end);
            partial.erase(0, end + 1);

            if(!readLine(line)) {
                return false;
            }
        }

        return true;
    };
    const auto start = Clock::now();
    const auto response = client.Post("/api/chat", httplib::Headers{}, request.dump(), "application/json", receive);

    if(!response && !stopped) {
        throw runtime_error("Cannot query Ollama: " + httplib::to_string(response.error()));
    } else if(response && response->status != 200) {
        throw runtime_error("Ollama returned HTTP " + std::to_string(response->status));
    }

    if(!stopped && !partial.empty()) {
        readLine(partial);
    }

    if(!report.is_object()) {
        throw runtime_error("Ollama returned invalid JSON");
    }

    logTiming("chat", start, report);
    report["message"] = {{"content", text}};
    return report;
}

void OllamaClient::sendPrerequest(const string& systemStart) {
    // One token is the least a generate request can ask for, and its answer is of no use.
    ordered_json request = {{"model", model},
                            {"stream", false},
                            {"raw", true},
                            {"prompt", "<|im_start|>system\n" + systemStart},
                            {"options", {{"temperature", 0}, {"num_predict", 1}}}};

    if(keepLoaded) {
        request["keep_alive"] = -1;
    }

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