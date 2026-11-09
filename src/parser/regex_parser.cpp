/**
 * @file
 * @brief Regex command interpretation without file, network, or execution side effects.
 */

#include "regex_parser.hpp"
#include "speech_text.hpp"
#include "home/resolver.hpp"

#include <set>

using Commands::Command;
using Commands::json;
using Grammar::Pattern;
using Grammar::Rule;
using std::nullopt;
using std::optional;
using std::regex;
using std::regex_replace;
using std::smatch;
using std::string;

namespace {
/**
 * @brief Remove courtesy words that never change a command, such as "please" or "could you".
 */
string stripPoliteness(const string& text) {
    static const regex prefix(R"(^(?:(?:please|hey|okay|ok) |(?:can|could|would) you (?:please )?)+)");
    static const regex suffix(R"( please$)");
    return regex_replace(regex_replace(text, prefix, ""), suffix, "");
}

/**
 * @brief Convert one captured number to the unit its slot expects.
 *
 * Volume is a 0..1 level; percent and temperature are whole amounts. A temperature may not
 * carry a percent suffix, and a trailing "degrees celsius" is ignored.
 *
 * @return Converted number, or nullopt when the words are not a valid amount.
 */
optional<double> slotNumber(const string& slot, string spoken) {
    static const regex degrees(R"( (?:degrees?)(?: celsius)?$)");
    spoken = regex_replace(spoken, degrees, "");
    const bool explicitPercent = Grammar::hasPercentSuffix(spoken);

    if(slot == "volume") {
        return Grammar::parseLevel(spoken);
    }

    if(slot == "temperature" && explicitPercent) {
        return nullopt;
    }

    const auto level = Grammar::parseLevel(explicitPercent ? spoken : spoken + " percent");
    return level ? optional<double>(*level * 100) : nullopt;
}

/**
 * @brief Collect the captured slot values of one match, with numbers converted.
 *
 * Numeric slots are also stored as "value" so templates can use either name. Word slots are
 * translated through the rule's values map.
 *
 * @return Slot values by name, or nullopt when a number is invalid.
 */
optional<json> captureSlots(const Rule& rule, const Pattern& pattern, const smatch& match) {
    json captured = json::object();

    for(const auto& [slot, group] : pattern.slots) {
        if(!match[group].matched) {
            continue;
        }

        const string spoken = match[group].str();

        if(slot == "volume" || slot == "percent" || slot == "temperature") {
            const auto number = slotNumber(slot, spoken);

            if(!number) {
                return nullopt;
            }

            captured[slot] = *number;
            captured["value"] = *number;
        } else {
            const auto word = Grammar::trim(spoken);
            captured[slot] = rule.values.value(word, word);
        }
    }

    return captured;
}

/**
 * @brief Fill a body template's "{slot}" values from captures.
 * @return Service data, or nullopt when the template needs a slot this match did not capture.
 */
optional<json> fillTemplate(const json& body, const json& captured) {
    json data = body;

    for(auto& field : data) {
        const string slot = Grammar::placeholderName(field);

        if(slot.empty()) {
            continue;
        }

        if(!captured.contains(slot)) {
            return nullopt;
        }

        field = captured[slot];
    }

    return data;
}

/**
 * @brief Fill a reply template's "{slot}" references from captured words.
 * @return Reply, or "" when it refers to a slot this match did not capture as a word.
 */
string fillReply(const string& reply, const json& captured) {
    static const regex reference(R"(\{([a-z_]+)\})");
    static const regex firstPerson(R"(\bmy\b)");
    string filled;
    auto copied = reply.cbegin();

    for(std::sregex_iterator match(reply.begin(), reply.end(), reference), end; match != end; ++match) {
        const auto value = captured.find((*match)[1].str());

        if(value == captured.end() || !value->is_string()) {
            return "";
        }

        // The reply speaks to the user, so a device they called "my phone" is "your phone".
        const auto word = (*match)[1].str() == "device" ? regex_replace(value->get<string>(), firstPerson, "your") : value->get<string>();
        filled.append(copied, (*match)[0].first).append(word);
        copied = (*match)[0].second;
    }

    return filled.append(copied, reply.cend());
}

/**
 * @brief The entity a captured device name means.
 *
 * With no name, or one like "it", the default device is used. A media command naming nothing may
 * also go on with an empty entity for the executor to reject; "it" with nothing to mean is left to
 * the next parser.
 *
 * @return Entity ID, or nullopt when the name is unknown, ambiguous, or spans more than one command.
 */
optional<string> capturedDevice(const Home::Resolver& devices, const string& spoken, const string& domain) {
    static const regex compound(R"(\b(then|but|to|by)\b)");
    const auto name = Grammar::normalizeName(spoken);

    if(name.empty() || Grammar::refersBack(name)) {
        const auto fallback = devices.defaultIn(domain);
        return !fallback.empty() || (name.empty() && domain == "media_player") ? optional<string>(fallback) : nullopt;
    } else if(std::regex_search(name, compound) || name.size() > 160) {
        return nullopt;
    }

    const auto found = devices.resolve(name, domain);
    return found ? optional<string>(found.entity) : nullopt;
}

/**
 * @brief Build the command one pattern produces for normalized text.
 * @param[in] deviceless Whether the rule's service acts on no device, so no device is resolved.
 * @return Command, or nullopt when the pattern does not match, a value is invalid, or the device
 *         cannot be resolved.
 */
optional<Command> matchPattern(const Rule& rule, const Pattern& pattern, const string& text, const Home::Resolver& devices, bool deviceless) {
    smatch match;

    if(!std::regex_match(text, match, pattern.expression)) {
        return nullopt;
    }

    const auto captured = captureSlots(rule, pattern, match);
    // A service that acts on no device, such as reading a queue, has no entity to resolve.
    const auto entity = !captured ? nullopt : deviceless ? optional<string>("") : capturedDevice(devices, captured->value("device", string()), rule.domain);

    if(!entity) {
        return nullopt;
    }

    const auto reply = fillReply(rule.reply, *captured);

    // A relative rule with a spoken amount ("up by ten") carries it for the executor to resolve.
    if(rule.relative && captured->contains("value")) {
        return Command{rule.domain, rule.service, *entity, {{"amount", captured->at("value")}}, reply};
    }

    const auto data = fillTemplate(rule.body, *captured);
    return data ? optional<Command>(Command{rule.domain, rule.service, *entity, *data, reply}) : nullopt;
}
} // namespace

RegexParser::RegexParser(const json& configuration) {
    configure(configuration);
}

void RegexParser::configure(const json& configuration) {
    auto compiled = Grammar::compileRules(configuration);
    Home::Registry configured(configuration);
    rules = std::move(compiled);
    registry = std::move(configured);
}

Commands::ParseResult RegexParser::parse(const string& text, const Commands::Context& context) {
    static const regex negativeNumber(R"(-\s*[0-9])");

    if(text.empty() || text.size() > 512 || std::regex_search(text, negativeNumber)) {
        return nullopt;
    }

    const string normalized = Grammar::normalizeText(text);

    if(Grammar::hasNegation(normalized) || Grammar::hasSequence(normalized)) {
        return nullopt;
    }

    const string request = stripPoliteness(normalized);
    const Home::Resolver devices(registry, context);
    optional<Command> chosen;
    std::set<string> matchedDomains;

    // The first matching pattern of each domain proposes a command; differing proposals from
    // several domains make the text ambiguous.
    for(const auto& rule : rules) {
        if(matchedDomains.count(rule.domain)) {
            continue;
        }

        const auto* fields = Commands::findService(context.services, rule.domain, rule.service);
        const bool deviceless = fields && !fields->targetsEntity;

        for(const auto& pattern : rule.patterns) {
            auto proposal = matchPattern(rule, pattern, request, devices, deviceless);

            if(!proposal) {
                continue;
            }

            if(chosen && *chosen != *proposal) {
                return nullopt;
            }

            chosen = std::move(proposal);
            matchedDomains.insert(rule.domain);
            break;
        }
    }

    return chosen ? Commands::ParseResult(*chosen) : Commands::ParseResult();
}