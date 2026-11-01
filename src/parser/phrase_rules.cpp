/**
 * @file
 * @brief Phrase DSL compilation and validation of configured command rules.
 */

#include "phrase_rules.hpp"
#include "speech_text.hpp"

#include <cctype>
#include <set>
#include <stdexcept>

namespace Grammar {
using std::regex;
using std::runtime_error;
using std::size_t;

namespace {
/**
 * @brief Translate phrase DSL text into regex source, numbering slot captures as they appear.
 *
 * {slot} becomes a lazy capture, [optional words] a noncapturing optional group, and (a|b) a
 * noncapturing alternative. Literal text is lowercased and regex metacharacters are escaped.
 *
 * @param[in] phrase DSL text, or an optional section during recursion.
 * @param[in,out] slots Slot capture indexes, extended as slots are found.
 * @throws std::runtime_error For unknown or repeated slots and malformed optional sections.
 */
string translatePhrase(const string& phrase, std::map<string, int>& slots) {
    static const std::set<string> allowedSlots = {"device",      "volume", "percent",
                                                  "temperature", "mode",   "option"};
    string result;

    for(size_t i = 0; i < phrase.size(); ++i) {
        const char c = phrase[i];

        if(c == '{') {
            const auto end = phrase.find('}', i);

            if(end == string::npos) {
                throw runtime_error("Unclosed phrase slot");
            }

            const string slot = phrase.substr(i + 1, end - i - 1);

            if(!allowedSlots.count(slot) || slots.count(slot)) {
                throw runtime_error("Unknown or repeated slot: " + slot);
            }

            slots[slot] = static_cast<int>(slots.size() + 1);
            result += "(.+?)";
            i = end;
        } else if(c == '[') {
            const auto end = phrase.find(']', i);

            if(end == string::npos || phrase.substr(i + 1, end - i - 1).find('{') != string::npos) {
                throw runtime_error("Optional phrase words need a closing ] and cannot contain slots");
            }

            std::map<string, int> unused;
            string optional = translatePhrase(phrase.substr(i + 1, end - i - 1), unused);

            // Move the separating space inside the optional group so "[the] x" also matches "x".
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

/**
 * @brief Compile one DSL phrase into a full-match pattern.
 * @throws std::runtime_error For an invalid length or DSL syntax.
 */
Pattern compilePhrase(const string& phrase) {
    if(phrase.empty() || phrase.size() > 512) {
        throw runtime_error("Phrase length must be 1..512 characters");
    }

    Pattern pattern;
    pattern.expression = regex(translatePhrase(phrase, pattern.slots), regex::ECMAScript | regex::optimize);
    return pattern;
}

/**
 * @brief Compile a rule's raw regex, mapping its configured capture groups to slots.
 * @throws std::runtime_error For an invalid length, capture group or value_type.
 */
Pattern compileRegex(const json& row, const string& ruleId) {
    const string expression = row.at("regex").get<string>();

    if(expression.empty() || expression.size() > 1024) {
        throw runtime_error("Regex length must be 1..1024: " + ruleId);
    }

    Pattern pattern{regex(expression, regex::ECMAScript | regex::optimize), {}};
    const int groups = static_cast<int>(pattern.expression.mark_count());
    const int device = row.value("device_group", 0);
    const int value = row.value("value_group", 0);
    const string type = row.value("value_type", string("volume"));

    if(device < 0 || value < 0 || device > groups || value > groups) {
        throw runtime_error("Capture group is out of range: " + ruleId);
    }

    if(value && type != "volume" && type != "percent" && type != "temperature") {
        throw runtime_error("Invalid value_type: " + ruleId);
    }

    if(device) {
        pattern.slots["device"] = device;
    }

    if(value) {
        pattern.slots[type] = value;
    }

    return pattern;
}

/**
 * @brief Require every body placeholder to be filled by a pattern.
 *
 * "{value}" is satisfied by any numeric slot: volume, percent or temperature.
 *
 * @throws std::runtime_error If a placeholder has no matching slot.
 */
void checkPlaceholders(const Pattern& pattern, const Rule& rule) {
    const bool hasNumber =
        pattern.slots.count("volume") || pattern.slots.count("percent") || pattern.slots.count("temperature");

    for(const auto& field : rule.body) {
        const string slot = placeholderName(field);

        if(!slot.empty() && !pattern.slots.count(slot) && !(slot == "value" && hasNumber)) {
            throw runtime_error("Body placeholder has no matching slot: " + rule.id + ": " + slot);
        }
    }
}

/**
 * @brief Require aliases to map entity IDs to lists of nonempty names.
 * @throws std::runtime_error For any other shape.
 */
void checkAliases(const json& aliases) {
    if(!aliases.is_object()) {
        throw runtime_error("aliases must map entity IDs to lists of spoken names");
    }

    for(const auto& [entity, names] : aliases.items()) {
        if(entity.find('.') == string::npos || !names.is_array()) {
            throw runtime_error("Alias entries need an entity ID and a list of names");
        }

        for(const auto& name : names) {
            if(!name.is_string() || normalizeName(name.get<string>()).empty()) {
                throw runtime_error("Alias names must be nonempty strings");
            }
        }
    }
}
} // namespace

string placeholderName(const json& value) {
    if(!value.is_string()) {
        return "";
    }

    const auto& text = value.get_ref<const string&>();
    return text.size() > 2 && text.front() == '{' && text.back() == '}' ? text.substr(1, text.size() - 2)
                                                                        : "";
}

std::vector<Rule> compileRules(const json& configuration) {
    if(!configuration.is_object() || configuration.value("version", 0) != 1 ||
       !configuration.contains("rules") || !configuration["rules"].is_array() ||
       configuration["rules"].empty() || configuration["rules"].size() > 256) {
        throw runtime_error("Phrase YAML needs version: 1 and 1..256 rules");
    }

    static const std::set<string> ruleFields = {"id",       "domain",       "service",     "phrases",
                                                "regex",    "device_group", "value_group", "value_type",
                                                "relative", "body",         "values"};
    std::vector<Rule> rules;
    std::set<string> ids;

    for(const auto& row : configuration["rules"]) {
        if(!row.is_object()) {
            throw runtime_error("Each rule must be a mapping");
        }

        for(const auto& field : row.items()) {
            if(!ruleFields.count(field.key())) {
                throw runtime_error("Unknown rule field: " + field.key());
            }
        }

        Rule rule;
        rule.id = row.at("id").get<string>();
        rule.domain = row.at("domain").get<string>();
        rule.service = row.at("service").get<string>();
        rule.body = row.value("body", json::object());
        rule.values = row.value("values", json::object());
        rule.relative = row.value("relative", false);
        const bool volumeStep =
            rule.domain == "media_player" && (rule.service == "volume_up" || rule.service == "volume_down");

        // Services are checked against Home Assistant's live catalogue when a command executes.
        if(rule.id.empty() || !ids.insert(rule.id).second || rule.domain.empty() || rule.service.empty() || !rule.body.is_object() ||
           rule.body.contains("entity_id") || !rule.values.is_object() || (rule.relative && !volumeStep)) {
            throw runtime_error("Invalid or duplicate phrase rule: " + rule.id);
        }

        if(row.contains("regex")) {
            rule.patterns.push_back(compileRegex(row, rule.id));
        }

        if(row.contains("phrases") && !row["phrases"].is_array()) {
            throw runtime_error("phrases must be a list: " + rule.id);
        }

        for(const auto& phrase : row.value("phrases", json::array())) {
            rule.patterns.push_back(compilePhrase(phrase.get<string>()));
        }

        if(rule.patterns.empty()) {
            throw runtime_error("Rule has no phrases or regex: " + rule.id);
        }

        for(const auto& pattern : rule.patterns) {
            checkPlaceholders(pattern, rule);
        }

        rules.push_back(std::move(rule));
    }

    checkAliases(configuration.value("aliases", json::object()));
    return rules;
}
} // namespace Grammar