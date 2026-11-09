/**
 * @file
 * @brief Turning requests into commands with an Ollama model.
 */

#include "ollama_parser.hpp"
#include "parser/device_names.hpp"
#include "parser/speech_text.hpp"

#include <algorithm>
#include <iterator>
#include <regex>
#include <set>
#include <spdlog/spdlog.h>
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
                         {{"domain", oneOf(domains)},
                          {"service", oneOf(services)},
                          {"heard", {{"type", "string"}}},
                          {"entity", oneOf(entities)},
                          {"data", {{"type", "object"}}},
                          {"reply", {{"type", "string"}}}}},
                        {"required", {"domain", "service", "heard", "entity", "data", "reply"}}};
}

/**
 * @brief Text without surrounding whitespace or trailing punctuation, as transcribers add both.
 */
string tidy(const string& text) {
    const auto first = text.find_first_not_of(" \t\r\n");
    const auto last = text.find_last_not_of(" \t\r\n.!?,");

    return first == string::npos || last < first ? string() : text.substr(first, last - first + 1);
}

/// What the model must do, one instruction per line.
const char* const instructions = R"(Turn the user's request into one command.
First choose the domain and one of its services for what is asked.
Then set heard to the exact words of the request that name the device, and entity to that domain's device whose name or
alias sounds most like them; speech is often misheard, so pick the closest device rather than giving up.
"turn on" and "turn off" mean turn_on and turn_off, while play and pause mean media_play and media_pause, so "play"
followed by a device's name plays that device rather than a song.
data holds only the service's allowed fields; never put the device, entity, domain or service in data, and use {} when
the service needs no fields.
volume_level is the spoken volume as a percentage: copy the number said, so "volume to 3" is 3 and "twenty percent" is
20.
"Up" or "down" without "to" means volume_up or volume_down, with the number said as amount, or {} when none is said.
A service marked "device": false acts on no device, so heard and entity are "".
reply is a few natural words to say aloud once the command succeeds, such as "Turning on the kitchen speaker."; use ""
for a service marked "device": false, whose answer comes from the service.
Make it natural, if the device is "my phone" reply with "your phone"
)";

/// Worked examples; small models follow these more reliably than rules alone.
const char* const examples = R"(Examples, using illustrative devices:
"turn on the kitchen speaker" -> {"domain": "media_player", "service": "turn_on", "heard": "kitchen speaker", "entity":
"media_player.kitchen", "data": {}, "reply": "Turning on the kitchen speaker."};
"play the kitchen speaker" -> {"domain": "media_player", "service": "media_play", "heard": "kitchen speaker", "entity":
"media_player.kitchen", "data": {}, "reply": "Playing the kitchen speaker."};
"set the desk ramp to forty percent" -> {"domain": "light", "service": "turn_on", "heard": "desk ramp", "entity":
"light.desk_lamp", "data": {"brightness_pct": 40}, "reply": "Setting the desk lamp to forty percent."}.
)";

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
bool soundsLike(const string& heard, const string& name, bool allowEdits = true) {
    if(heard == name) {
        return true;
    } else if(heard.size() < 3 || name.size() < 3) {
        return false;
    } else if(heard.compare(0, name.size(), name) == 0 || name.compare(0, heard.size(), heard) == 0) {
        return true;
    } else if(!allowEdits) {
        return false;
    }

    const auto limit = std::min<size_t>(3, (std::max(heard.size(), name.size()) + 1) / 2);
    return Grammar::editDistance(heard, name, limit) <= limit;
}

/**
 * @brief How many heard words match a word of the device's friendly name, object ID or aliases.
 * @param[in] allowEdits Whether a misheard word counts, rather than only a whole or partial word.
 */
size_t nameScore(const vector<string>& heard, const Device& device, const json& aliases, bool allowEdits = true) {
    static const set<string> fillers = {"the", "and", "of", "to", "in", "my"};
    auto names = words(device.name);
    const auto objectId = words(device.entityId.substr(device.entityId.find('.') + 1));
    names.insert(names.end(), objectId.begin(), objectId.end());

    for(const auto& alias : aliases.value(device.entityId, json::array())) {
        const auto aliasWords = words(alias.get<string>());
        names.insert(names.end(), aliasWords.begin(), aliasWords.end());
    }

    size_t score = 0;

    for(const auto& word : heard) {
        const auto matches = [&](const string& name) { return soundsLike(word, name, allowEdits); };

        if(!fillers.count(word) && std::any_of(names.begin(), names.end(), matches)) {
            ++score;
        }
    }

    return score;
}

/**
 * @brief Whether words the model says it heard are really in the request.
 *
 * Small models may complete a word from the device list, writing "q990b" for "q990", so each word
 * may also be the start of a request word or the other way round. Edit distance is too loose
 * here, as it would let "bedroom" pass for "bathroom".
 */
bool inRequest(const string& heard, const string& request) {
    if(Grammar::normalizeText(request).find(heard) != string::npos) {
        return true;
    }

    const auto said = words(request);
    const auto heardWords = words(heard);

    return std::all_of(heardWords.begin(), heardWords.end(), [&](const string& word) {
        return std::any_of(said.begin(), said.end(), [&](const string& other) {
            const auto shared = std::min(word.size(), other.size());
            return shared >= 3 && word.compare(0, shared, other, 0, shared) == 0;
        });
    });
}

/**
 * @brief A domain as it would be said aloud, such as "media player".
 */
string domainWords(string domain) {
    std::replace(domain.begin(), domain.end(), '_', ' ');
    return domain;
}

/**
 * @brief Point the command at the device the request names, or say why it names none.
 *
 * The heard words must occur in the request. The model chooses the domain and service well but
 * often the wrong device within it, so the device of that domain matching the most heard words
 * replaces the model's choice; a tie between two such devices is ambiguous. A device of another
 * domain matching more of the words means the model chose the wrong domain. A request naming no
 * device may only target the context's default device.
 *
 * @return Why no device is backed by the request, in words fit to say aloud, or "" once command.entity names one.
 */
string groundDevice(const string& heard, const string& request, Command& command, const Context& context, const json& aliases) {
    if(heard.empty()) {
        return !context.defaultDevice.empty() && command.entity == context.defaultDevice ? "" : "You didn't name a device";
    } else if(!inRequest(heard, request)) {
        return "I didn't catch which device you meant";
    }

    const auto heardWords = words(heard);
    vector<const Device*> best;
    size_t bestScore = 0;
    size_t otherScore = 0;
    size_t closeScore = 0;
    size_t otherCloseScore = 0;

    for(const auto& device : context.devices) {
        const auto score = nameScore(heardWords, device, aliases);
        const auto close = nameScore(heardWords, device, aliases, false);

        if(device.domain != command.domain) {
            otherScore = std::max(otherScore, score);
            otherCloseScore = std::max(otherCloseScore, close);
            continue;
        }

        closeScore = std::max(closeScore, close);

        if(score > bestScore) {
            best = {&device};
            bestScore = score;
        } else if(score == bestScore) {
            best.push_back(&device);
        }
    }

    const auto chosen = std::find_if(best.begin(), best.end(), [&](const Device* device) { return device->entityId == command.entity; });

    if(bestScore == 0) {
        return "I can't find a " + domainWords(command.domain) + " device called " + heard;
    } else if(otherScore > bestScore || otherCloseScore > closeScore) {
        return heard + " isn't a " + domainWords(command.domain) + " device";
    } else if(chosen == best.end() && best.size() > 1) {
        return heard + " matches more than one " + domainWords(command.domain) + " device";
    } else if(chosen == best.end()) {
        spdlog::debug("Correcting the model's device {} to {}", command.entity, best[0]->entityId);
        command.entity = best[0]->entityId;
    }

    return "";
}

/**
 * @brief Whether a request sounds like moving playback somewhere, rather than powering something.
 */
bool movesPlayback(const string& request) {
    static const regex power(R"(\b(?:turn|switch|power)(?: .+)? (?:on|off)$|\b(?:turn|switch|power) (?:on|off)\b)");
    static const regex verb(R"(\b(?:cast|move|transfer|send|put|switch|change|play|playing|playback|listen|continue)\b)");
    static const regex place(R"(\b(?:to|on|onto|through|over)\b)");
    const auto text = Grammar::normalizeText(request);
    return !regex_search(text, power) && regex_search(text, verb) && regex_search(text, place);
}

/**
 * @brief What the model may choose from, both as prompt text and as schema limits.
 */
struct Offer {

    /// Commandable devices with their names and aliases.
    json devices = json::array();

    /// Each offered domain's services, with their fields and descriptions.
    json services = json::object();

    /// Domains the reply may name.
    set<string> domains;

    /// Entities the reply may name.
    set<string> entities;

    /// Services the reply may name, across all offered domains.
    set<string> serviceNames;
};

/**
 * @brief Collect the devices and services a request may refer to.
 */
Offer describeOffer(const Context& context, const json& aliases) {
    Offer offer;

    // Offer only devices whose domain the catalogue can command.
    for(const auto& device : context.devices) {
        if(context.services.count(device.domain)) {
            offer.devices.push_back({{"entity", device.entityId}, {"name", device.name}, {"domain", device.domain}});

            if(aliases.contains(device.entityId)) {
                offer.devices.back()["aliases"] = aliases[device.entityId];
            }

            offer.domains.insert(device.domain);
            offer.entities.insert(device.entityId);
        }
    }

    // A domain with services that need no device, such as reading a music queue, is offered regardless.
    for(const auto& [domain, domainServices] : context.services) {
        for(const auto& entry : domainServices) {
            if(!entry.second.targetsEntity) {
                offer.domains.insert(domain);
            }
        }
    }

    // Describe each offered domain's services by their allowed and required data fields.
    for(const auto& domain : offer.domains) {
        for(const auto& [service, fields] : context.services.at(domain)) {
            auto& described = offer.services[domain][service];
            described = {{"allowed", fields.allowed}, {"required", fields.required}};

            if(!fields.targetsEntity) {
                described["device"] = false;
            }

            if(!fields.description.empty()) {
                described["description"] = fields.description;
            }

            offer.serviceNames.insert(service);
        }
    }

    return offer;
}

/**
 * @brief The system and user messages for a request.
 */
json buildMessages(const string& request, const Offer& offer) {
    return {{{"role", "system"},
             {"content", string(instructions) + examples +
                             "Services with their allowed and required data fields: " + offer.services.dump() +
                             ". Devices: " + offer.devices.dump() +
                             ". A service's description says how people ask for it; prefer the service whose description "
                             "matches the wording. If no service fits the request, return empty strings."}},
            {{"role", "user"}, {"content", request}}};
}

/**
 * @brief Turn a Spotify device named under the wrong domain, such as media_player.select_source, into a cast.
 */
void correctWrongDomain(Command& command, const string& request, const Context& context) {
    const auto named = [&](const Device& device) { return device.entityId == command.entity; };
    const auto target = std::find_if(context.devices.begin(), context.devices.end(), named);

    if(target != context.devices.end() && target->domain == "spotify" && command.domain != "spotify" && movesPlayback(request)) {
        spdlog::debug("Correcting the model's {}.{} to spotify.cast", command.domain, command.service);
        command.domain = "spotify";
        command.service = "cast";
    }
}

/**
 * @brief Why a request must not run as this one command, or "" when it may.
 *
 * Models drop a "don't" and act on the rest, or do the first of two things and forget the other.
 * Words the model read as a name or free text are left out, so "Never Gonna Give You Up" is a song
 * and "bed and bath" a device. A service acting on no device can't half-finish, so "and" is fine.
 */
string objectionTo(const Command& command, const string& heard, const string& request, const Commands::ServiceFields& fields) {
    static const regex joined("\\band\\b");
    auto text = Grammar::normalizeText(request);
    vector<string> named = {Grammar::normalizeText(heard)};

    for(const auto& value : command.data) {
        if(value.is_string()) {
            named.push_back(Grammar::normalizeText(value.get<string>()));
        }
    }

    for(const auto& said : named) {
        const auto found = said.empty() ? string::npos : text.find(said);

        if(found != string::npos) {
            text.erase(found, said.size());
        }
    }

    if(Grammar::hasNegation(text)) {
        return "I won't act on a request that says not to";
    }

    return Grammar::hasSequence(text) || (fields.targetsEntity && regex_search(text, joined)) ? "I can only do one thing at a time" : "";
}

/**
 * @brief Reduce the data to the fields the service accepts, as small models often echo the device into it.
 */
void keepAllowedFields(Command& command, const Commands::ServiceFields& fields) {
    if(command.data.is_null()) {
        command.data = json::object();
    }

    for(auto field = command.data.begin(); field != command.data.end();) {
        if(fields.allowed.count(field.key())) {
            ++field;
        } else {
            spdlog::debug("Dropping unexpected data field: {}", field.key());
            field = command.data.erase(field);
        }
    }
}

/**
 * @brief Make a volume command relative when the request has a direction but no "to".
 *
 * Small models pick volume_set for "turn it up 3".
 */
void applyVolumeDirection(Command& command, const string& request) {
    static const regex direction("\\b(up|down|louder|quieter|softer|raise|lower)\\b");
    static const regex to("\\bto\\b");
    static const regex downward("\\b(down|quieter|softer|lower)\\b");
    const auto normalized = Grammar::normalizeText(request);
    const bool relative = regex_search(normalized, direction) && !regex_search(normalized, to);

    if(command.domain == "media_player" && relative && command.service.rfind("volume_", 0) == 0) {
        const json level = command.data.value("volume_level", command.data.value("amount", json()));
        command.service = regex_search(normalized, downward) ? "volume_down" : "volume_up";
        command.data = level.is_number() ? json{{"amount", level}} : json::object();
    }
}

/**
 * @brief Whether a percentage appears in the request, or the request has no digits to compare it with.
 *
 * Numbers transcribed as digits must be copied exactly; small models "correct" 300 to 30.
 */
bool wasSpoken(double percent, const string& request) {
    static const regex digits("\\b\\d+(\\.\\d+)?\\b"); // Standalone, so "Q990" is not a number
    bool spoken = true;

    for(std::sregex_iterator match(request.begin(), request.end(), digits), end; match != end; ++match) {
        spoken = false;

        if(std::stod(match->str()) == percent) {
            return true;
        }
    }

    return spoken;
}

/**
 * @brief Convert the percentages the model copied into 0..1 levels, keeping arithmetic out of the model.
 * @return False when a percentage was not in the request, so the command must be rejected.
 */
bool convertPercentages(Command& command, const string& request) {
    for(const char* field : {"volume_level", "amount"}) {
        if(!command.data.contains(field) || !command.data[field].is_number()) {
            continue;
        }

        const auto percent = command.data[field].get<double>();

        if(!wasSpoken(percent, request)) {
            spdlog::warn("Rejecting {} {}: not a number in the request", field, percent);
            return false;
        }

        command.data[field] = percent / 100;
    }

    return true;
}
} // namespace

void OllamaParser::configure(const json& configuration) {
    const auto configured = configuration.value("aliases", json::object());

    if(!configured.is_object()) {
        throw runtime_error("aliases must map entity IDs to lists of spoken names");
    }

    for(const auto& [entity, names] : configured.items()) {
        if(!names.is_array() || !std::all_of(names.begin(), names.end(), [](const json& name) { return name.is_string(); })) {
            throw runtime_error("aliases for " + entity + " must be a list of names");
        }
    }

    aliases = configured;
}

Commands::ParseResult OllamaParser::parse(const string& text, const Context& context) {
    const auto request = tidy(text);

    if(request.empty()) {
        return nullopt;
    }

    const auto offer = describeOffer(context, aliases);

    try {
        const auto reply = client.chat(buildMessages(request, offer), json::array(), commandFormat(offer.domains, offer.serviceNames, offer.entities));
        const auto content = json::parse(reply.at("message").at("content").get<string>());
        auto command = content.get<Command>();
        command.reply = content.value("reply", "");
        correctWrongDomain(command, request, context);
        const auto* fields = Commands::findService(context.services, command.domain, command.service);

        if(command.domain.empty() || !fields) {
            return nullopt;
        }

        if(fields->targetsEntity) {
            const auto unbacked = groundDevice(normalizeName(content.at("heard").get<string>()), request, command, context, aliases);

            if(!unbacked.empty()) {
                spdlog::warn("Rejecting model's device choice: {}", unbacked);
                return Commands::ParseResult::refused(unbacked);
            }
        } else {
            command.entity = "";
        }

        keepAllowedFields(command, *fields);

        const auto objection = objectionTo(command, content.value("heard", ""), request, *fields);

        if(!objection.empty()) {
            spdlog::warn("Rejecting request \"{}\": {}", request, objection);
            return Commands::ParseResult::refused(objection);
        }

        applyVolumeDirection(command, request);

        if(!convertPercentages(command, request)) {
            return Commands::ParseResult::refused("That number doesn't match what you said");
        }

        return command;
    } catch(const std::exception& error) {
        spdlog::error("Ollama parse failed: {}", error.what());
        return Commands::ParseResult::refused("I'm sorry sir, I'm having a hard time thinking at the moment.");
    }
}