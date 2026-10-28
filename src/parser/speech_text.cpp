/**
 * @file
 * @brief Transcript normalization and strict spoken-number parsing.
 */

#include "speech_text.hpp"

#include <cctype>
#include <cmath>
#include <regex>
#include <sstream>
#include <unordered_map>

namespace Grammar {
using std::nullopt;
using std::optional;
using std::regex;

namespace {
/**
 * @brief Pattern for a trailing " percent", " per cent" or "%".
 */
const regex& percentSuffix() {
    static const regex pattern(R"((?: percent| per cent|%)$)");
    return pattern;
}

/**
 * @brief Values of the number words that make up zero..ninety-nine.
 */
const std::unordered_map<string, int>& numberWords() {
    static const std::unordered_map<string, int> words = {
        {"zero", 0},     {"one", 1},      {"two", 2},        {"three", 3},     {"four", 4},
        {"five", 5},     {"six", 6},      {"seven", 7},      {"eight", 8},     {"nine", 9},
        {"ten", 10},     {"eleven", 11},  {"twelve", 12},    {"thirteen", 13}, {"fourteen", 14},
        {"fifteen", 15}, {"sixteen", 16}, {"seventeen", 17}, {"eighteen", 18}, {"nineteen", 19},
        {"twenty", 20},  {"thirty", 30},  {"forty", 40},     {"fifty", 50},    {"sixty", 60},
        {"seventy", 70}, {"eighty", 80},  {"ninety", 90}};
    return words;
}
} // namespace

string trim(const string& text) {
    const auto first = text.find_first_not_of(' ');
    return first == string::npos ? "" : text.substr(first, text.find_last_not_of(' ') - first + 1);
}

string normalizeText(const string& text) {
    string result;
    result.reserve(text.size());

    for(unsigned char c : text) {
        if(c == '&') {
            result += " and ";
        } else if(c == '-' || c == ',' || c == '!' || c == '?' || std::isspace(c)) {
            result += ' ';
        } else {
            result += static_cast<char>(std::tolower(c));
        }
    }

    // Remove sentence punctuation, preserving decimal points and entity IDs.
    result = trim(result);

    while(!result.empty() && result.back() == '.') {
        result.pop_back();
    }

    static const regex spaces(R"(\s+)");
    return trim(std::regex_replace(result, spaces, " "));
}

string normalizeName(const string& name) {
    string result = normalizeText(name);

    if(result.compare(0, 4, "the ") == 0) {
        result.erase(0, 4);
    }

    if(result.size() > 2 && result.compare(result.size() - 2, 2, "'s") == 0) {
        result.resize(result.size() - 2);
    }

    return trim(result);
}

bool hasPercentSuffix(const string& text) {
    return std::regex_search(text, percentSuffix());
}

optional<double> parseLevel(string text) {
    text = trim(text);
    const bool percent = hasPercentSuffix(text);
    text = trim(std::regex_replace(text, percentSuffix(), ""));
    static const regex digits(R"((?:[0-9]+(?:\.[0-9]+)?|\.[0-9]+))");

    if(std::regex_match(text, digits)) {
        double value = 0;

        try {
            value = std::stod(text);
        } catch(...) {
            return nullopt;
        }

        // Bare decimals up to one are levels; whole numbers and larger values are percentages.
        if(percent || text.find('.') == string::npos || value > 1) {
            value /= 100;
        }

        return std::isfinite(value) && value >= 0 && value <= 1 ? optional<double>(value) : nullopt;
    }

    const auto& words = numberWords();
    const auto point = text.find("point ");

    // "<whole> point <digit words>": "zero point four" is 0.4, "twenty point five percent" is 0.205.
    if(point != string::npos) {
        const string whole = trim(text.substr(0, point));
        double value = 0;

        if(!whole.empty()) {
            const auto wholeLevel = parseLevel(whole + " percent");

            if(!wholeLevel) {
                return nullopt;
            }

            value = *wholeLevel * 100;
        }

        if(!percent && value > 1) {
            return nullopt;
        }

        double place = 0.1;
        bool anyDigit = false;
        std::istringstream digitWords(text.substr(point + 6));

        // Each word after "point" is one more decimal digit.
        for(string word; digitWords >> word; place *= 0.1) {
            const auto digit = words.find(word);

            if(digit == words.end() || digit->second > 9) {
                return nullopt;
            }

            value += digit->second * place;
            anyDigit = true;
        }

        if(percent) {
            value /= 100;
        }

        return anyDigit && value <= 1 ? optional<double>(value) : nullopt;
    }

    if(text == "half") {
        return 0.5;
    }

    if(text == "full" || text == "maximum" || text == "max" || text == "one hundred" || text == "a hundred") {
        return 1.0;
    }

    if(text == "minimum" || text == "min") {
        return 0.0;
    }

    const auto single = words.find(text);

    if(single != words.end()) {
        return single->second / 100.0;
    }

    // Compound "<tens> <units>", such as "forty two".
    const auto space = text.find(' ');

    if(space == string::npos) {
        return nullopt;
    }

    const auto tens = words.find(text.substr(0, space));
    const auto units = words.find(text.substr(space + 1));

    if(tens == words.end() || units == words.end() || tens->second < 20 || tens->second % 10 != 0 ||
       units->second < 1 || units->second > 9) {
        return nullopt;
    }

    return (tens->second + units->second) / 100.0;
}
} // namespace Grammar