/**
 * @file
 * @brief Transcript normalization and strict spoken-number parsing.
 */

#include "speech_text.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <iterator>
#include <regex>
#include <sstream>
#include <unordered_map>
#include <vector>

namespace Grammar {
using std::istringstream;
using std::max;
using std::min;
using std::nullopt;
using std::optional;
using std::regex;
using std::regex_replace;
using std::vector;

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
    return trim(regex_replace(result, spaces, " "));
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

std::size_t editDistance(const string& a, const string& b, std::size_t limit) {
    if(a.size() > b.size() + limit || b.size() > a.size() + limit) {
        return limit + 1;
    }

    std::vector<std::size_t> previous(b.size() + 1), current(b.size() + 1);

    for(std::size_t j = 0; j <= b.size(); ++j) {
        previous[j] = j;
    }

    // Two-row dynamic programming over insertions, deletions and substitutions.
    for(std::size_t i = 1; i <= a.size(); ++i) {
        current[0] = i;
        std::size_t rowBest = i;

        for(std::size_t j = 1; j <= b.size(); ++j) {
            current[j] = min({previous[j] + 1, current[j - 1] + 1, previous[j - 1] + (a[i - 1] != b[j - 1])});
            rowBest = min(rowBest, current[j]);
        }

        if(rowBest > limit) {
            return limit + 1;
        }

        previous.swap(current);
    }

    return previous[b.size()];
}

vector<string> nameWords(string name) {
    std::replace(name.begin(), name.end(), '_', ' ');
    istringstream stream(normalizeName(name));
    return {std::istream_iterator<string>(stream), {}};
}

bool hasPercentSuffix(const string& text) {
    return std::regex_search(text, percentSuffix());
}

optional<double> parseLevel(string text) {
    text = trim(text);
    const bool percent = hasPercentSuffix(text);
    text = trim(regex_replace(text, percentSuffix(), ""));
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
        istringstream digitWords(text.substr(point + 6));

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

bool hasNegation(const string& text) {
    static const regex negation(R"(\b(?:not|never|dont|don't)\b)");
    return std::regex_search(text, negation);
}

bool hasSequence(const string& text) {
    static const regex sequence(R"(\b(?:then|but)\b)");
    return std::regex_search(text, sequence);
}

bool isNonSpeech(const string& text) {
    static const regex labels(R"(\([^)]*\)|\[[^\]]*\]|\*[^*]*\*)");
    // Laughter, hums and hesitations, allowing drawn-out spellings such as "hmmm" and "uhhh".
    static const regex filler(R"((?:ha|he|hi|ho)+h?|m+(?:h+m+)*|h+m+|u+[hm]+|h+u+h+|e+r+m*|e+h+|a+h+|o+h*)");
    istringstream stream(normalizeText(regex_replace(text, labels, " ")));
    string word;

    while(stream >> word) {
        // Keep only letters, so "ha." and "\"hmm\"" are judged as words.
        word.erase(std::remove_if(word.begin(), word.end(), [](unsigned char c) { return !std::isalpha(c); }), word.end());

        if(!word.empty() && !std::regex_match(word, filler)) {
            return false;
        }
    }

    return true;
}

optional<string> stripWakeWord(const string& text, const string& wakePhrase) {
    istringstream wakeStream(normalizeText(wakePhrase)), textStream(normalizeText(text));
    const vector<string> wake{std::istream_iterator<string>(wakeStream), {}};
    const vector<string> words{std::istream_iterator<string>(textStream), {}};

    if(wake.empty()) {
        return text;
    }

    static const vector<string> greetings = {"hey", "hi", "ok", "okay", "oi", "yo"};
    const size_t start = !words.empty() && std::count(greetings.begin(), greetings.end(), words[0]) ? 1 : 0;
    string target;

    for(const auto& word : wake) {
        target += word;
    }

    const auto limit = min<size_t>(3, max<size_t>(1, (target.size() + 1) / 2));

    size_t bestCount = 0, bestEdits = limit + 1;

    // Compare the phrase without spaces against one word fewer, as many, and one more, so a phrase
    // Whisper splits ("jar vis") or merges still matches; the closest span wins.
    for(size_t count = max<size_t>(1, wake.size() - 1); count <= wake.size() + 1 && start + count <= words.size(); ++count) {
        string heard;

        for(size_t i = start; i < start + count; ++i) {
            heard += words[i];
        }

        const auto edits = editDistance(heard, target, limit);

        if(edits < bestEdits) {
            bestEdits = edits;
            bestCount = count;
        }
    }

    if(bestCount == 0) {
        return nullopt;
    }

    string rest;

    for(size_t i = start + bestCount; i < words.size(); ++i) {
        rest += (rest.empty() ? "" : " ") + words[i];
    }

    return rest;
}
} // namespace Grammar