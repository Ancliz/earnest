#include "detail.hpp"

#include <cctype>
#include <cmath>
#include <regex>
#include <sstream>
#include <unordered_map>

namespace Parser::Detail {
using std::istringstream;
using std::regex;
using std::string;
using std::unordered_map;

string trim(const string& text) {
    const auto first = text.find_first_not_of(' ');
    return first == string::npos ? "" : text.substr(first, text.find_last_not_of(' ') - first + 1);
}

string normalize(const string& input) {
    string result;
    result.reserve(input.size());

    for(unsigned char c : input) {
        if(c == '&') {
            result += " and ";
        } else if(c == '-' || std::isspace(c)) {
            result += ' ';
        } else if(c == ',' || c == '!' || c == '?') {
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

// Deliberately strict number grammar: prevents junk or another command being swallowed.
bool level(string text, double& value) {
    text = trim(text);
    bool percent = false;

    for(const string suffix : {" percent", " per cent", "%"}) {
        if(text.size() >= suffix.size() && text.substr(text.size() - suffix.size()) == suffix) {
            percent = true;
            text = trim(text.substr(0, text.size() - suffix.size()));
            break;
        }
    }

    static const regex digits(R"((?:[0-9]+(?:\.[0-9]+)?|\.[0-9]+))");

    if(std::regex_match(text, digits)) {
        try {
            value = std::stod(text);
        } catch(...) {
            return false;
        }

        // Bare decimals below one are normalized levels; whole numbers are percentages.
        if(percent || text.find('.') == string::npos || value > 1) {
            value /= 100;
        }

        return std::isfinite(value) && value >= 0 && value <= 1;
    }

    static const unordered_map<string, int> small = {
        {"zero", 0},     {"one", 1},      {"two", 2},        {"three", 3},     {"four", 4},
        {"five", 5},     {"six", 6},      {"seven", 7},      {"eight", 8},     {"nine", 9},
        {"ten", 10},     {"eleven", 11},  {"twelve", 12},    {"thirteen", 13}, {"fourteen", 14},
        {"fifteen", 15}, {"sixteen", 16}, {"seventeen", 17}, {"eighteen", 18}, {"nineteen", 19},
        {"twenty", 20},  {"thirty", 30},  {"forty", 40},     {"fifty", 50},    {"sixty", 60},
        {"seventy", 70}, {"eighty", 80},  {"ninety", 90}};

    const auto point = text.find("point ");

    if(point != string::npos) {
        const auto whole = trim(text.substr(0, point));

        double wholeValue = 0;

        if(!whole.empty() && !level(whole + " percent", wholeValue)) {
            return false;
        }

        value = wholeValue * 100;

        if(!percent && value > 1) {
            return false;
        }

        double place = 0.1;
        istringstream words(text.substr(point + 6));
        string word;
        bool any = false;

        while(words >> word) {
            const auto digit = small.find(word);

            if(digit == small.end() || digit->second > 9) {
                return false;
            }

            value += digit->second * place;
            place *= 0.1;
            any = true;
        }

        if(percent) {
            value /= 100;
        }

        return any && value <= 1;
    }

    if(text == "half") {
        value = 0.5;
        return true;
    }

    if(text == "full" || text == "maximum" || text == "max" || text == "one hundred" || text == "a hundred") {
        value = 1;
        return true;
    }

    if(text == "minimum" || text == "min") {
        value = 0;
        return true;
    }

    const auto found = small.find(text);

    if(found != small.end()) {
        value = found->second / 100.0;
        return true;
    }

    const auto space = text.find(' ');

    if(space == string::npos) {
        return false;
    }

    const auto tens = small.find(text.substr(0, space));
    const auto units = small.find(text.substr(space + 1));

    const auto isValidCompoundNumber = [&] {
        return tens != small.end() && units != small.end() && tens->second >= 20 && tens->second % 10 == 0 &&
               units->second >= 1 && units->second <= 9;
    };

    if(!isValidCompoundNumber()) {
        return false;
    }

    value = (tens->second + units->second) / 100.0;
    return true;
}

} // namespace Parser::Detail