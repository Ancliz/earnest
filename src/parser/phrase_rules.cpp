/**
 * @file
 * @brief Phrase DSL translation and validation of compiled command rules.
 */

#include "detail.hpp"
#include "home_assistant/services.hpp"

#include <cctype>
#include <set>
#include <stdexcept>

namespace Parser::Detail {
using std::size_t;

/**
 * @brief Translate phrase DSL text into a regex source while assigning capture indexes.
 *
 * Named slots become lazy captures, optional sections become noncapturing groups,
 * and alternatives retain their grouping without changing slot numbering. Literal
 * metacharacters are escaped and text bytes are lowercased. The caller compiles
 * the returned source and enforces the top-level phrase length limit.
 *
 * @param[in] phrase DSL source, or an optional-section substring during recursion.
 * @param[in,out] slots Named-slot map extended as captures are encountered.
 * @return Regex source string; anchors are supplied by full-match usage.
 * @throws std::runtime_error For unknown/repeated slots or malformed optional sections.
 * @note On failure, slots may contain captures visited before the error; callers use
 *       a candidate map and do not expose partially compiled patterns.
 */
string compilePhrase(const string& phrase, std::map<string, int>& slots) {
    string result;

    for(size_t i = 0; i < phrase.size(); ++i) {
        const char c = phrase[i];

        if(c == '{') {
            const auto end = phrase.find('}', i);

            if(end == string::npos) {
                throw std::runtime_error("Unclosed phrase slot");
            }

            const string slot = phrase.substr(i + 1, end - i - 1);
            static const std::set<string> allowed = {"device",      "volume", "percent",
                                                     "temperature", "mode",   "option"};

            if(!allowed.count(slot) || slots.count(slot)) {
                throw std::runtime_error("Unknown or repeated slot: " + slot);
            }

            slots[slot] = static_cast<int>(slots.size() + 1);
            result += "(.+?)";
            i = end;
        } else if(c == '[') {
            const auto end = phrase.find(']', i);

            if(end == string::npos || phrase.substr(i + 1, end - i - 1).find('{') != string::npos) {
                throw std::runtime_error("Optional phrase words need a closing ] and cannot contain slots");
            }

            std::map<string, int> unused;
            string optional = compilePhrase(phrase.substr(i + 1, end - i - 1), unused);

            if(end + 1 < phrase.size() && phrase[end + 1] == ' ') {
                optional += ' ';
                i = end + 1;
            } else {
                if(!result.empty() && result.back() == ' ') {
                    result.pop_back();
                    optional = " " + optional;
                }

                i = end;
            }

            result += "(?:" + optional + ")?";
        } else if(c == '(') {
            result += "(?:";
        } else if(c == ')' || c == '|') {
            result += c;
        } else {
            if(string(R"(\.^$*+?{}])").find(c) != string::npos) {
                result += '\\';
            }

            result += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
    }

    return result;
}

Pattern phrasePattern(const string& phrase) {
    if(phrase.empty() || phrase.size() > 512) {
        throw std::runtime_error("Phrase length must be 1..512 characters");
    }

    Pattern pattern;
    const string expression = compilePhrase(phrase, pattern.slots);
    pattern.expression = regex(expression, regex::ECMAScript | regex::optimize);
    return pattern;
}

void validatePattern(const Pattern& pattern, const Rule& rule) {
    for(const auto& field : rule.body.items()) {
        if(!field.value().is_string()) {
            continue;
        }

        const string value = field.value().get<string>();

        if(value.size() > 2 && value.front() == '{' && value.back() == '}') {
            const string key = value.substr(1, value.size() - 2);
            const bool numericValue =
                key == "value" && (pattern.slots.count("volume") || pattern.slots.count("percent") ||
                                   pattern.slots.count("temperature"));

            if(!pattern.slots.count(key) && !numericValue) {
                throw std::runtime_error("Body placeholder has no matching slot: " + rule.id + ": " + key);
            }
        }
    }
}

std::vector<Rule> compileRules(const json& config) {
    if(!config.is_object() || config.value("version", 0) != 1 || !config.contains("rules") ||
       !config["rules"].is_array() || config["rules"].empty() || config["rules"].size() > 256) {
        throw std::runtime_error("Phrase YAML needs version: 1 and 1..256 rules");
    }

    static const std::set<string> rootFields = {"version", "aliases", "advantage_air", "rules"};

    for(const auto& field : config.items()) {
        if(!rootFields.count(field.key())) {
            throw std::runtime_error("Unknown YAML setting: " + field.key());
        }
    }

    std::vector<Rule> rules;
    std::set<string> ids;

    for(const auto& row : config["rules"]) {
        static const std::set<string> ruleFields = {"id",       "domain",       "service",     "phrases",
                                                    "regex",    "device_group", "value_group", "value_type",
                                                    "relative", "body",         "values"};

        if(!row.is_object()) {
            throw std::runtime_error("Each rule must be a mapping");
        }

        for(const auto& field : row.items()) {
            if(!ruleFields.count(field.key())) {
                throw std::runtime_error("Unknown rule field: " + field.key());
            }
        }

        Rule rule;
        rule.id = row.at("id").get<string>();
        rule.domain = row.at("domain").get<string>();
        rule.service = row.at("service").get<string>();
        rule.body = row.value("body", json::object());
        rule.values = row.value("values", json::object());
        rule.relative = row.value("relative", false);

        if(rule.id.empty() || !ids.insert(rule.id).second || !HomeServices::find(rule.domain, rule.service) ||
           !rule.body.is_object() || rule.body.contains("entity_id") || !rule.values.is_object() ||
           (rule.relative && (rule.domain != "media_player" ||
                              (rule.service != "volume_up" && rule.service != "volume_down")))) {
            throw std::runtime_error("Invalid or duplicate phrase rule: " + rule.id);
        }

        if(row.contains("regex")) {
            const string expression = row.at("regex").get<string>();

            if(expression.empty() || expression.size() > 1024) {
                throw std::runtime_error("Regex length must be 1..1024: " + rule.id);
            }

            Pattern pattern{regex(expression, regex::ECMAScript | regex::optimize), {}};
            const int device = row.value("device_group", 0);
            const int value = row.value("value_group", 0);

            if(device < 0 || value < 0 || device > static_cast<int>(pattern.expression.mark_count()) ||
               value > static_cast<int>(pattern.expression.mark_count())) {
                throw std::runtime_error("Capture group is out of range: " + rule.id);
            }

            if(device) {
                pattern.slots["device"] = device;
            }

            if(value) {
                const string type = row.value("value_type", string("volume"));

                if(type != "volume" && type != "percent" && type != "temperature") {
                    throw std::runtime_error("Invalid value_type: " + rule.id);
                }

                pattern.slots[type] = value;
            }

            rule.patterns.push_back(std::move(pattern));
        }

        if(row.contains("phrases")) {
            if(!row["phrases"].is_array()) {
                throw std::runtime_error("phrases must be a list: " + rule.id);
            }

            for(const auto& phrase : row["phrases"]) {
                rule.patterns.push_back(phrasePattern(phrase.get<string>()));
            }
        }

        if(rule.patterns.empty()) {
            throw std::runtime_error("Rule has no phrases or regex: " + rule.id);
        }

        for(const auto& pattern : rule.patterns) {
            validatePattern(pattern, rule);
        }

        rules.push_back(std::move(rule));
    }

    if(config.contains("aliases") && !config["aliases"].is_object()) {
        throw std::runtime_error("aliases must map entity IDs to lists of spoken names");
    }

    if(config.contains("aliases")) {
        for(const auto& entry : config["aliases"].items()) {
            if(entry.key().find('.') == string::npos || !entry.value().is_array()) {
                throw std::runtime_error("Alias entries need an entity ID and a list of names");
            }

            for(const auto& alias : entry.value()) {
                if(!alias.is_string() || normalizeDeviceName(alias.get<string>()).empty()) {
                    throw std::runtime_error("Alias names must be nonempty strings");
                }
            }
        }
    }

    return rules;
}

} // namespace Parser::Detail