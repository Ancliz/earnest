/**
 * @file
 * @brief Turning requests into commands with an Ollama model.
 */

#include "ollama_parser.hpp"
#include "log/log.hpp"
#include "answer_stream.hpp"
#include "home/resolver.hpp"
#include "parser/speech_text.hpp"

#include <algorithm>
#include <regex>
#include <set>
#include <spdlog/spdlog.h>

using Commands::Command;
using Commands::Context;
using Commands::Device;
using Commands::ParseResult;
using Grammar::normalizeName;
using nlohmann::json;
using nlohmann::ordered_json;
using std::nullopt;
using std::regex;
using std::regex_search;
using std::set;
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
 * @brief Schema for a reply that is only words to say.
 */
ordered_json answerFormat() {
    return ordered_json{{"type", "object"}, {"properties", {{"answer", {{"type", "string"}}}}}, {"required", ordered_json::array({"answer"})}};
}

/**
 * @brief Schema that lets the reply be either one command or only words to say.
 */
ordered_json replyFormat(ordered_json command) {
    return ordered_json{{"anyOf", ordered_json::array({std::move(command), answerFormat()})}};
}

/**
 * @brief What the start of a reply shows it to be.
 */
enum class Route { Undecided, Command, Answer, Silent };

/**
 * @brief Tell a command from an answer by the reply's first field; an empty answer means staying silent.
 */
Route routeOf(const string& generated) {
    static const regex command(R"(^\s*\{\s*"domain")");
    static const regex answer(R"(^\s*\{\s*"answer"\s*:\s*"([\s\S]))");
    std::smatch opening;

    if(regex_search(generated, command)) {
        return Route::Command;
    } else if(!regex_search(generated, opening, answer)) {
        return Route::Undecided;
    }

    return opening[1] == "\"" ? Route::Silent : Route::Answer;
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
const char* const instructions = R"(You are this home's voice assistant, talking with the person who lives here.
If a listed service can do or answer the request, turn it into one command, even a question such as what is playing.
Otherwise reply with only an answer: one or two short natural sentences to say aloud, calling the user "sir".
Questions, jokes, stories and small talk are all welcome, so answer those yourself.
An answer changes nothing in the house, so never say you did something or are about to.
You can't see the time, the weather or whether a device is on, so say you don't know rather than guess.
Use "" for words that make no sense or aren't meant for you.
For a command, first choose the domain and one of its services for what is asked.
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
"light.desk_lamp", "data": {"brightness_pct": 40}, "reply": "Setting the desk lamp to forty percent."};
"what's playing" -> {"domain": "spotify", "service": "get_queue", "heard": "", "entity": "", "data": {}, "reply": ""};
"how long until wonderwall" -> {"domain": "spotify", "service": "time_until", "heard": "", "entity": "", "data": {"song":
"wonderwall"}, "reply": ""};
"what's the capital of france" -> {"answer": "Paris, sir."};
"how are you" -> {"answer": "Very well, thank you, sir."}.
)";

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

    const auto said = Grammar::nameWords(request);
    const auto heardWords = Grammar::nameWords(heard);

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
 * often the wrong device within it, so the resolver's choice replaces the model's, which only
 * settles ties. Words pointing back are settled here, not by the model, which often picks the wrong
 * earlier device: "it" is the context's default device, or the latest in the conversation of this
 * domain, and "the other one" the latest that isn't the default. A heard that is empty, or names
 * an earlier device the request doesn't, only counts as pointing back if the request does; it
 * can't move a command the request aimed elsewhere.
 *
 * @return Why no device is backed by the request, in words fit to say aloud, or "" once command.entity names one.
 */
string groundDevice(const string& heard, const string& request, Command& command, const Context& context, const Home::Resolver& resolver) {
    using Outcome = Home::Resolution::Outcome;

    static const regex otherDevice("\\b(?:other|same)\\b");
    static const regex pointer("\\b(?:it|that|this|them|other|same|again|music|sound|audio|volume)\\b");
    const auto remembered = resolver.defaultIn(command.domain);
    const bool other = regex_search(heard, otherDevice);
    const bool unsaid = heard.empty() || !inRequest(heard, request);
    const bool meantEarlier = unsaid && (command.entity.empty() || command.entity == remembered || regex_search(Grammar::normalizeText(request), pointer));

    if(other || meantEarlier || Grammar::refersBack(heard)) {
        command.entity = other ? "" : remembered;

        // Newest first, so "the other one" is the last device in this domain that isn't the current one.
        for(auto past = context.history.rbegin(); command.entity.empty() && past != context.history.rend(); ++past) {
            if(past->command.domain == command.domain && !(other && past->command.entity == remembered)) {
                command.entity = past->command.entity;
            }
        }

        return !command.entity.empty() ? "" : other ? "I didn't catch which device you meant" : "You didn't name a device";
    } else if(heard.empty()) {
        return "You didn't name a device";
    } else if(!inRequest(heard, request)) {
        return "I didn't catch which device you meant";
    }

    const auto found = resolver.resolve(heard, command.domain, Home::Reach::Words, command.entity);

    if(found.outcome == Outcome::Unknown) {
        return "I can't find a " + domainWords(command.domain) + " device called " + heard;
    } else if(found.outcome == Outcome::OtherDomain) {
        return heard + " isn't a " + domainWords(command.domain) + " device";
    } else if(found.outcome == Outcome::Ambiguous) {
        return heard + " matches more than one " + domainWords(command.domain) + " device";
    }

    if(found.entity != command.entity) {
        Log::think()->debug("Correcting the model's device {} to {}", command.entity, found.entity);
        command.entity = found.entity;
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
Offer describeOffer(const Context& context, const Home::Registry& registry) {
    Offer offer;

    // Offer only devices whose domain the catalogue can command.
    for(const auto& device : context.devices) {
        if(context.services.count(device.domain)) {
            offer.devices.push_back({{"entity", device.entityId}, {"name", device.name}, {"domain", device.domain}});

            const auto aliases = registry.aliasesOf(device.entityId);

            if(!aliases.empty()) {
                offer.devices.back()["aliases"] = aliases;
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
 * @brief An earlier request's reply as the model would have written it, so the conversation reads as its own.
 */
string pastReply(const Commands::PastRequest& past) {
    if(!past.answer.empty()) {
        return json{{"answer", past.answer}}.dump();
    }

    const auto& done = past.command;
    const ordered_json reply = {{"domain", done.domain}, {"service", done.service}, {"heard", ""}, {"entity", done.entity}, {"data", done.data}, {"reply", past.succeeded ? "" : "That failed."}};
    return reply.dump();
}

/**
 * @brief The part of the system message that is the same for every request while the devices are.
 */
string stablePrompt(const Offer& offer) {
    return string(instructions) + examples + "Services with their allowed and required data fields: " + offer.services.dump() +
           ". Devices: " + offer.devices.dump() +
           ". A service's description says how people ask for it; prefer the service whose description "
           "matches the wording. If no service fits the request, answer instead.";
}

/**
 * @brief The messages for a request: the instructions, then the conversation so far as real turns.
 *
 * Earlier requests as user and assistant turns were followed far better than a summary in the
 * instructions: 49 of 60 follow-ups against 24. They also come after the unchanging instructions.
 *
 * @param[in] stable What stablePrompt() returned.
 */
json buildMessages(const string& stable, const string& request, const vector<Commands::PastRequest>& history) {
    json messages = {{{"role", "system"}, {"content", stable}}};

    for(const auto& past : history) {
        messages.push_back({{"role", "user"}, {"content", past.request}});
        messages.push_back({{"role", "assistant"}, {"content", pastReply(past)}});
    }

    messages.push_back({{"role", "user"}, {"content", request}});
    return messages;
}

/**
 * @brief Move a service named under a domain that lacks it to the only domain offering it.
 *
 * The schema limits domains and services separately, so a small model can pair them wrongly.
 */
void correctServiceDomain(Command& command, const Context& context) {
    vector<string> owners;

    for(const auto& [domain, services] : context.services) {
        if(services.count(command.service)) {
            owners.push_back(domain);
        }
    }

    if(owners.size() == 1 && owners[0] != command.domain) {
        Log::think()->debug("Correcting the model's {}.{} to domain {}", command.domain, command.service, owners[0]);
        command.domain = owners[0];
    }
}

/**
 * @brief Turn a Spotify device named under the wrong domain, such as media_player.select_source, into a cast.
 */
void correctWrongDomain(Command& command, const string& request, const Context& context) {
    const auto named = [&](const Device& device) { return device.entityId == command.entity; };
    const auto target = std::find_if(context.devices.begin(), context.devices.end(), named);

    if(target != context.devices.end() && target->domain == "spotify" && command.domain != "spotify" && movesPlayback(request)) {
        Log::think()->debug("Correcting the model's {}.{} to spotify.cast", command.domain, command.service);
        command.domain = "spotify";
        command.service = "cast";
    }
}

/**
 * @brief Why a request must not run as this one command, or "" when it may.
 *
 * Models drop a "don't" and act on the rest, or do the first of two things and forget the other.
 * Words the model read as a name or free text are left out, so "Never Gonna Give You Up" is a song
 * and "bed and bath" a device. A service acting on no device can't half-finish, so "and" is fine,
 * and neither can a follow-up opening with it, such as "and the q990".
 */
string objectionTo(const Command& command, const string& heard, const string& request, const Commands::ServiceFields& fields) {
    static const regex joined("\\band\\b");
    static const regex followingOn(R"(^(?:and|also) )");
    auto text = std::regex_replace(Grammar::normalizeText(request), followingOn, "");
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
            Log::think()->debug("Dropping unexpected data field: {}", field.key());
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
            Log::think()->warn("Rejecting {} {}: not a number in the request", field, percent);
            return false;
        }

        command.data[field] = percent / 100;
    }

    return true;
}
} // namespace

void OllamaParser::configure(const json& configuration) {
    registry = Home::Registry(configuration);
}

ParseResult OllamaParser::speakAnswer(const json& messages, const Context& context) {
    AnswerStream answer(context.onSentence);
    const auto reply = client.chat(messages, answerFormat(), answerTemperature, [&](const string& piece) {
        answer.feed(piece);
        return true;
    });
    answer.finish();

    if(answer.text().empty() && reply.value("done_reason", "") == "length") {
        throw std::runtime_error("the answer was cut off before its first sentence ended");
    }

    return ParseResult::answered(answer.text());
}

ParseResult OllamaParser::parse(const string& text, const Context& context) {
    const auto request = tidy(text);

    if(request.empty()) {
        return nullopt;
    }

    const auto offer = describeOffer(context, registry);

    try {
        const auto stable = stablePrompt(offer);

        if(prerequest) {
            client.sendPrerequest(stable);
        }

        const auto messages = buildMessages(stable, request, context.history);
        auto route = Route::Undecided;
        string seen;

        // Always chosen at temperature 0, so only the wording of an answer is ever left to chance.
        const auto reply = client.chat(messages, replyFormat(commandFormat(offer.domains, offer.serviceNames, offer.entities)), 0, [&](const string& piece) {
            seen += piece;
            route = route == Route::Undecided ? routeOf(seen) : route;
            return route == Route::Undecided || route == Route::Command;
        });

        if(route == Route::Answer) {
            return speakAnswer(messages, context);
        } else if(route == Route::Silent) {
            return ParseResult::answered("");
        }

        const auto content = json::parse(reply.at("message").at("content").get<string>());
        auto command = content.get<Command>();
        command.reply = content.value("reply", "");
        correctServiceDomain(command, context);
        correctWrongDomain(command, request, context);
        const auto* fields = Commands::findService(context.services, command.domain, command.service);

        if(command.domain.empty() || !fields) {
            return nullopt;
        }

        if(fields->targetsEntity) {
            const auto unbacked = groundDevice(normalizeName(content.at("heard").get<string>()), request, command, context, Home::Resolver(registry, context));

            if(!unbacked.empty()) {
                Log::think()->warn("Rejecting model's device choice: {}", unbacked);
                return ParseResult::refused(unbacked);
            }
        } else {
            command.entity = "";
        }

        keepAllowedFields(command, *fields);

        const auto objection = objectionTo(command, content.value("heard", ""), request, *fields);

        if(!objection.empty()) {
            Log::think()->warn("Rejecting request \"{}\": {}", request, objection);
            return ParseResult::refused(objection);
        }

        applyVolumeDirection(command, request);

        if(!convertPercentages(command, request)) {
            return ParseResult::refused("That number doesn't match what you said");
        }

        return command;
    } catch(const std::exception& error) {
        Log::think()->error("Ollama parse failed: {}", error.what());
        return ParseResult::refused("I'm sorry sir, I'm having a hard time thinking at the moment.");
    }
}