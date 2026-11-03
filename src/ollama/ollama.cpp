/**
 * @file
 * @brief Ollama chat requests.
 */

#include "ollama.hpp"
#include "ollama_parser.hpp"
#include "parser/device_names.hpp"
#include "parser/speech_text.hpp"

#include <algorithm>
#include <spdlog/spdlog.h>
#include <iterator>
#include <limits>
#include <regex>
#include <set>
#include <sstream>

using Commands::Command;
using Commands::Context;
using Commands::Device;
using Grammar::normalizeName;
using nlohmann::json;
using nlohmann::ordered_json;
using std::nullopt;
using std::regex;
using std::regex_search;
using std::runtime_error;
using std::set;
using std::size_t;
using std::string;
using std::vector;

namespace {
/**
 * @brief Schema that limits the reply to one command naming a known domain, service and entity.
 *
 * Fields are generated in schema order: first the words heard as the device, then the entity they
 * name. Each enumeration ends with "" so the model can say that nothing fits.
 */
ordered_json commandFormat(const set<string>& domains, const set<string>& services, const set<string>& entities) {
    /*
     * @brief A string property restricted to the given values or "".
     */
    const auto oneOf = [](const set<string>& values) {
        auto allowed = ordered_json::array();

        for(const auto& value : values) {
            allowed.push_back(value);
        }

        allowed.push_back("");
        return ordered_json{{"type", "string"}, {"enum", allowed}};
    };

    return ordered_json{{"type", "object"},
                        {"properties",
                         {{"heard", {{"type", "string"}}},
                          {"entity", oneOf(entities)},
                          {"domain", oneOf(domains)},
                          {"service", oneOf(services)},
                          {"data", {{"type", "object"}}}}},
                        {"required", {"heard", "entity", "domain", "service", "data"}}};
}

/**
 * @brief Text without surrounding whitespace or trailing punctuation, as transcribers add both.
 */
string tidy(const string& text) {
    const auto first = text.find_first_not_of(" \t\r\n");
    const auto last = text.find_last_not_of(" \t\r\n.!?,");

    return first == string::npos || last < first ? string() : text.substr(first, last - first + 1);
}

/// Worked examples; small models follow these more reliably than rules alone.
const char* const examples =
    "Examples, using illustrative devices: \"turn on the kitchen speaker\" -> {\"heard\": \"kitchen speaker\", "
    "\"entity\": \"media_player.kitchen\", \"domain\": \"media_player\", \"service\": \"turn_on\", \"data\": {}}; "
    "\"set the desk ramp to forty percent\" -> {\"heard\": \"desk ramp\", \"entity\": \"light.desk_lamp\", "
    "\"domain\": \"light\", \"service\": \"turn_on\", \"data\": {\"brightness_pct\": 40}}. ";

/**
 * @brief Words of a name, normalized; underscores count as spaces so entity IDs split too.
 */
vector<string> words(string name) {
    std::replace(name.begin(), name.end(), '_', ' ');
    std::istringstream stream(normalizeName(name));
    return {std::istream_iterator<string>(stream), {}};
}

/**
 * @brief Whether a heard word could be a misheard or shortened name word.
 *
 * Short words must match exactly. Longer ones may differ by about a third, capped at three
 * edits, so "ramp" matches "lamp" and model number "q919" matches "q990b" but "queue" misses "q800b"; a
 * three-letter start also matches, so "bedroom" matches "bed".
 */
bool soundsLike(const string& heard, const string& name) {
    if(heard == name) {
        return true;
    } else if(heard.size() < 3 || name.size() < 3) {
        return false;
    } else if(heard.compare(0, name.size(), name) == 0 || name.compare(0, heard.size(), heard) == 0) {
        return true;
    }

    const auto limit = std::min<size_t>(3, (std::max(heard.size(), name.size()) + 1) / 2);
    return Grammar::editDistance(heard, name, limit) <= limit;
}

/**
 * @brief How many heard words match a word of the device's friendly name or object ID.
 */
size_t nameScore(const vector<string>& heard, const Device& device) {
    static const set<string> fillers = {"the", "and", "of", "to", "in"};
    auto names = words(device.name);
    const auto objectId = words(device.entityId.substr(device.entityId.find('.') + 1));
    names.insert(names.end(), objectId.begin(), objectId.end());
    size_t score = 0;

    for(const auto& word : heard) {
        const auto matches = [&](const string& name) { return soundsLike(word, name); };

        if(!fillers.count(word) && std::any_of(names.begin(), names.end(), matches)) {
            ++score;
        }
    }

    return score;
}

/**
 * @brief Why the model's device choice is not backed by the request, or "" when it is.
 *
 * The heard words must occur in the request, at least one must sound like the chosen device's
 * name, and no other device may match more of them. A request naming no device may only target
 * the context's default device.
 */
string checkHeard(const string& heard, const string& request, const string& entity, const Context& context) {
    if(heard.empty()) {
        return !context.defaultDevice.empty() && entity == context.defaultDevice ? "" : "no device named";
    } else if(Grammar::normalizeText(request).find(heard) == string::npos) {
        return "\"" + heard + "\" is not in the request";
    }

    const auto chosen = std::find_if(context.devices.begin(), context.devices.end(),
                                     [&](const Device& device) { return device.entityId == entity; });

    if(chosen == context.devices.end()) {
        return "unknown entity " + entity;
    }

    const auto heardWords = words(heard);
    const auto score = nameScore(heardWords, *chosen);

    if(score == 0) {
        return "\"" + heard + "\" does not sound like " + chosen->name;
    }

    // A device matching more of the heard words means the model picked the wrong one.
    for(const auto& device : context.devices) {
        if(nameScore(heardWords, device) > score) {
            return "\"" + heard + "\" sounds more like " + device.name + " than " + chosen->name;
        }
    }

    return "";
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

    return reply;
}

void OllamaParser::configure(const json&) {}

std::optional<Command> OllamaParser::parse(const string& text, const Context& context) {
    const auto request = tidy(text);

    if(request.empty()) {
        return nullopt;
    }

    json devices = json::array();
    set<string> domains;
    set<string> entities;

    // Offer only devices whose domain the catalogue can command.
    for(const auto& device : context.devices) {
        if(context.services.count(device.domain)) {
            devices.push_back(
                {{"entity", device.entityId}, {"name", device.name}, {"domain", device.domain}});
            domains.insert(device.domain);
            entities.insert(device.entityId);
        }
    }

    // Describe each offered domain's services by their allowed and required data fields.
    json services = json::object();
    set<string> serviceNames;

    for(const auto& domain : domains) {
        for(const auto& [service, fields] : context.services.at(domain)) {
            services[domain][service] = {{"allowed", fields.allowed}, {"required", fields.required}};
            serviceNames.insert(service);
        }
    }

    const json messages = {
        {{"role", "system"},
         {"content", "Turn the user's request into one Home Assistant command. "
                     "Set heard to the exact words of the request that name the device, and entity to "
                     "the device whose name sounds most like them; speech is often misheard, so pick "
                     "the closest device rather than giving up. Set domain to that "
                     "device's domain. Set service to one of that domain's services: \"turn on\" and "
                     "\"turn off\" mean turn_on and turn_off, while play and pause mean media_play and "
                     "media_pause. data holds only the service's allowed fields; never put the device, "
                     "entity, domain or service in data, and use {} when the service needs no fields. "
                     "volume_level is the spoken volume as a percentage: copy the number said, so "
                     "\"volume to 3\" is 3 and \"twenty percent\" is 20. \"Up\" or \"down\" without \"to\" "
                     "means volume_up or volume_down, with the number said as amount, or {} when none is said. " +
                         string(examples) +
                         "Services with their allowed and required data fields: " + services.dump() +
                         ". Devices: " + devices.dump() + ". If the request is not a device command, return empty strings."}},
        {{"role", "user"}, {"content", request}}};

    try {
        const auto reply =
            client.chat(messages, json::array(), commandFormat(domains, serviceNames, entities));
        const auto content = json::parse(reply.at("message").at("content").get<string>());
        auto command = content.get<Command>();
        const auto* fields = Commands::findService(context.services, command.domain, command.service);

        if(command.domain.empty() || !fields) {
            return nullopt;
        }

        const auto unbacked = checkHeard(normalizeName(content.at("heard").get<string>()), request, command.entity, context);

        if(!unbacked.empty()) {
            spdlog::warn("Rejecting model's device choice: {}", unbacked);
            return nullopt;
        }

        if(command.data.is_null()) {
            command.data = json::object();
        }

        // Small models often echo the device into data; keep only the fields the service accepts.
        for(auto field = command.data.begin(); field != command.data.end();) {
            if(fields->allowed.count(field.key())) {
                ++field;
            } else {
                spdlog::debug("Dropping unexpected data field: {}", field.key());
                field = command.data.erase(field);
            }
        }

        // Small models pick volume_set for "turn it up 3"; a direction without "to" is a relative change.
        const regex direction("\\b(up|down|louder|quieter|softer|raise|lower)\\b");
        const auto normalized = Grammar::normalizeText(request);
        const bool relative = regex_search(normalized, direction) && !regex_search(normalized, regex("\\bto\\b"));

        if(command.domain == "media_player" && relative && command.service.rfind("volume_", 0) == 0) {
            const bool down = regex_search(normalized, regex("\\b(down|quieter|softer|lower)\\b"));
            const json level = command.data.value("volume_level", command.data.value("amount", json()));
            command.service = down ? "volume_down" : "volume_up";
            command.data = level.is_number() ? json{{"amount", level}} : json::object();
        }

        // The model copies spoken percentages; converting them here keeps arithmetic out of the model.
        for(const char* field : {"volume_level", "amount"}) {
            if(!command.data.contains(field) || !command.data[field].is_number()) {
                continue;
            }

            const auto percent = command.data[field].get<double>();
            const regex digits("\\b\\d+(\\.\\d+)?\\b"); // Standalone, so "Q990" is not a number
            bool spoken = true;

            // Numbers transcribed as digits must be copied exactly; small models "correct" 300 to 30.
            for(std::sregex_iterator match(request.begin(), request.end(), digits), end; match != end; ++match) {
                spoken = false;

                if(std::stod(match->str()) == percent) {
                    spoken = true;
                    break;
                }
            }

            if(!spoken) {
                spdlog::warn("Rejecting {} {}: not a number in the request", field, percent);
                return nullopt;
            }

            command.data[field] = percent / 100;
        }

        return command;
    } catch(const std::exception& error) {
        spdlog::error("Ollama parse failed: {}", error.what());
        return nullopt;
    }
}