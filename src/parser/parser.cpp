/**
 * @file
 * @brief Regex command interpretation without file, network, or execution side effects.
 */

#include "parser.hpp"
#include "detail.hpp"

#include <regex>
#include <set>
#include <stdexcept>

namespace Parser {
using Detail::compileRules;
using Detail::level;
using Detail::normalize;
using Detail::phrasePattern;
using Detail::resolveDevice;
using Detail::Rule;
using std::regex;
using std::smatch;

struct RegexParser::Impl {
    json config;
    std::vector<Rule> rules;
};

struct OllamaParser::Impl {
    json config;
    std::vector<Rule> rules;
};

RegexParser::RegexParser() : impl(std::make_unique<Impl>()) {}

RegexParser::RegexParser(const json& configuration) : RegexParser() {
    configure(configuration);
}

RegexParser::~RegexParser() = default;

void RegexParser::configure(const json& configuration) {
    auto next = std::make_unique<Impl>();
    next->config = configuration;
    next->rules = compileRules(configuration);
    impl.swap(next);
}

void RegexParser::addPhrase(const string& ruleId, const string& phrase) {
    auto pattern = phrasePattern(phrase);

    for(auto& rule : impl->rules) {
        if(rule.id == ruleId) {
            Detail::validatePattern(pattern, rule);
            rule.patterns.push_back(std::move(pattern));
            return;
        }
    }

    throw std::runtime_error("Unknown rule: " + ruleId);
}

json RegexParser::parse(const string& input, const Options& baseOptions) const {
    if(input.empty() || input.size() > 512) {
        return nullptr;
    }

    static const regex negative(R"(-\s*[0-9])");
    static const regex unsafe(R"(\b(?:not|never|dont|don't|then|but)\b)");
    static const regex prefix(R"(^(?:(?:please|hey|okay|ok) |(?:can|could|would) you (?:please )?)+)");
    static const regex suffix(R"( please$)");
    auto text = normalize(input);

    if(std::regex_search(input, negative) || std::regex_search(text, unsafe)) {
        return nullptr;
    }

    text = std::regex_replace(std::regex_replace(text, prefix, ""), suffix, "");
    Options options = baseOptions;

    if(impl->config.contains("aliases")) {
        for(const auto& entry : impl->config["aliases"].items()) {
            const string entity = entry.key();

            if(!options.deviceStates.count(entity)) {
                continue;
            }

            const string domain = entity.substr(0, entity.find('.'));

            for(const auto& alias : entry.value()) {
                auto& map = options.domainDevices[domain];
                const string name = normalizeDeviceName(alias.get<string>());
                const auto existing = map.emplace(name, entity);

                if(!existing.second && existing.first->second != entity) {
                    existing.first->second.clear();
                }
            }
        }
    }

    json candidate = nullptr;
    std::set<string> matchedDomains;

    for(const auto& rule : impl->rules) {
        if(matchedDomains.count(rule.domain)) {
            continue;
        }

        for(const auto& pattern : rule.patterns) {
            smatch match;

            if(!std::regex_match(text, match, pattern.expression)) {
                continue;
            }

            json captured = json::object();
            bool valid = true;
            bool hasNumber = false;

            for(const auto& slot : pattern.slots) {
                if(!match[slot.second].matched) {
                    continue;
                }

                string value = match[slot.second].str();

                if(slot.first == "volume" || slot.first == "percent" || slot.first == "temperature") {
                    static const regex degrees(R"( (?:degrees?)(?: celsius)?$)");
                    value = std::regex_replace(value, degrees, "");
                    double number;
                    const bool normalized = slot.first == "volume";
                    static const regex percentSuffix(R"((?: percent| per cent|%)$)");
                    const bool explicitPercent = std::regex_search(value, percentSuffix);
                    valid = !(slot.first == "temperature" && explicitPercent) &&
                            level(normalized || explicitPercent ? value : value + " percent", number);

                    if(!valid) {
                        break;
                    }

                    captured[slot.first] = normalized ? number : number * 100;
                    captured["value"] = captured[slot.first];
                    hasNumber = true;
                } else {
                    if(rule.values.contains(value)) {
                        value = rule.values[value].get<string>();
                    }

                    captured[slot.first] = value;
                }
            }

            json target;

            if(!valid || !resolveDevice(captured.value("device", string()), rule.domain, options, target)) {
                continue;
            }

            json result;

            if(rule.relative && hasNumber) {
                const double amount =
                    captured.at("value").get<double>() * (rule.service == "volume_down" ? -1 : 1);
                result = {{"endpoint", "/commands/media_player/adjust_volume"},
                          {"body", {{"entity_id", target}, {"amount", amount}}}};
            } else {
                json body = rule.body;

                for(auto& item : body.items()) {
                    if(item.value().is_string()) {
                        const string value = item.value().get<string>();

                        if(value.size() > 2 && value.front() == '{' && value.back() == '}') {
                            const string key = value.substr(1, value.size() - 2);

                            if(!captured.contains(key)) {
                                valid = false;
                                break;
                            }

                            item.value() = captured[key];
                        }
                    }
                }

                if(!valid) {
                    continue;
                }

                body["entity_id"] = target;
                result = {{"endpoint", "/services/" + rule.domain + "/" + rule.service}, {"body", body}};
            }

            if(!candidate.is_null() && candidate != result) {
                return nullptr;
            }

            candidate = result;
            matchedDomains.insert(rule.domain);
            break;
        }
    }

    return candidate;
}



void OllamaParser::configure(const json& configuration) {
    impl->config = configuration;
}

json OllamaParser::parse(const string& input, const Options& options) const {
    if(input.empty() || input.size() > 512) {
        return nullptr;
    }

    // auto devices = options.devices.
}

// void get_keys(unorderd_map<string, string>& map) {

// }

} // namespace Parser