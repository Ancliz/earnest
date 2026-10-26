#pragma once
#include "parser.hpp"
#include <map>
#include <regex>
#include <vector>

namespace Parser::Detail {
using std::regex;

struct Pattern {
    regex expression;
    std::map<string, int> slots;
};

struct Rule {
    string id;
    string domain;
    string service;
    json body;
    json values;
    bool relative = false;
    std::vector<Pattern> patterns;
};

void validatePattern(const Pattern& pattern, const Rule& rule);
Pattern phrasePattern(const string& phrase);
std::vector<Rule> compileRules(const json& config);
std::string trim(const std::string& text);
std::string normalize(const std::string& input);
bool level(std::string text, double& value);
bool resolveDevice(const string& captured, const string& domain, const Options& options, json& target);
} // namespace Parser::Detail

nlohmann::json readYaml(const std::string& text);