#include "parser.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
#include <regex>
#include <sstream>
#include <vector>

namespace Parser {

using std::istringstream;
using std::numeric_limits;
using std::regex;
using std::size_t;
using std::smatch;
using std::vector;

namespace {

string trim(const string& text) {
    const auto first = text.find_first_not_of(' ');
    return first == string::npos ? "" : text.substr(first, text.find_last_not_of(' ') - first + 1);
}

string normalize(const string& input) {
    string result;
    result.reserve(input.size());

    for(unsigned char c : input) {
        if(c == '-' || std::isspace(c)) result += ' ';
        else if(c == ',' || c == '!' || c == '?') result += ' ';
        else result += static_cast<char>(std::tolower(c));
    }

    // Remove sentence punctuation, preserving decimal points and entity IDs.
    result = trim(result);
    while(!result.empty() && result.back() == '.')
        result.pop_back();
    static const regex spaces(R"(\s+)");
    return trim(std::regex_replace(result, spaces, " "));
}

// Bounded edit distance; called only for configured device aliases after a rule matches.
size_t distance(const string& a, const string& b, size_t limit) {
    if(a.size() > b.size() + limit || b.size() > a.size() + limit) {
        return limit + 1;
    }

    vector<size_t> previous(b.size() + 1), current(b.size() + 1);

    for(size_t j = 0; j <= b.size(); ++j) {
        previous[j] = j;
    }

    for(size_t i = 1; i <= a.size(); ++i) {
        current[0] = i;
        size_t best = i;

        for(size_t j = 1; j <= b.size(); ++j) {
            current[j] = std::min({previous[j] + 1, current[j - 1] + 1,
                previous[j - 1] + (a[i - 1] != b[j - 1])});
            best = std::min(best, current[j]);
        }

        if(best > limit) {
            return limit + 1;
        }

        previous.swap(current);
    }

    return previous[b.size()];
}

bool resolveDevice(const string& captured, const Options& options, json& target) {
    const auto name = normalizeDeviceName(captured);
    target = nullptr;
    if(name.empty()) {
        return true;
    }

    // Do not silently interpret a multi-command utterance as a single device name.
    static const regex compound(R"(\b(and|then|but|to|by)\b)");
    if(std::regex_search(name, compound) || name.size() > 160) {
        return false;
    }

    if(options.devices.empty()) {
        target = name;
        return true;
    }

    size_t best = numeric_limits<size_t>::max();
    string match;
    bool ambiguous = false;

    for(const auto& entry : options.devices) {
        const auto alias = normalizeDeviceName(entry.first);
        const auto limit = std::min(name.size(), alias.size()) >= 8 ? 2u : 1u;
        const auto edits = alias == name ? 0 : options.fuzzyDevices && name.size() >= 4
            ? distance(name, alias, limit) : limit + 1;

        if(edits > limit) {
            continue;
        }

        if(edits < best) {
            best = edits;
            match = entry.second;
            ambiguous = false;
        } else if(edits == best && match != entry.second) {
            ambiguous = true;
        }
    }

    if(best == numeric_limits<size_t>::max() || ambiguous || match.empty()) {
        return false;
    }

    target = match;
    return true;
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
        try         { value = std::stod(text); }
        catch (...) { return false;            }

        // Bare decimals below one are normalized levels; whole numbers are percentages.
        if(percent || text.find('.') == string::npos || value > 1) {
            value /= 100;
        }

        return std::isfinite(value) && value >= 0 && value <= 1;
    }

    static const unordered_map<string, int> small = {
        {"zero",0},{"one",1},{"two",2},{"three",3},{"four",4},{"five",5},{"six",6},
        {"seven",7},{"eight",8},{"nine",9},{"ten",10},{"eleven",11},{"twelve",12},
        {"thirteen",13},{"fourteen",14},{"fifteen",15},{"sixteen",16},{"seventeen",17},
        {"eighteen",18},{"nineteen",19},{"twenty",20},{"thirty",30},{"forty",40},
        {"fifty",50},{"sixty",60},{"seventy",70},{"eighty",80},{"ninety",90}
    };

    const auto point = text.find("point ");

    if(point != string::npos) {
        const auto whole = trim(text.substr(0, point));

        if(!whole.empty() && whole != "zero" && whole != "one") {
            return false;
        }

        value = whole == "one" ? 1 : 0;
        double place = 0.1;
        istringstream words(text.substr(point + 6));
        string word;
        bool any = false;

        while(words >> word) {
            const auto digit = small.find(word);
            if (digit == small.end() || digit->second > 9) return false;
            value += digit->second * place;
            place *= 0.1;
            any = true;
        }

        if(percent) {
            value /= 100;
        }

        return any && value <= 1;
    }
    if(text == "half") { value = 0.5; return true; }
    if(text == "full" || text == "maximum" || text == "max" || text == "one hundred" || text == "a hundred") {
        value = 1; return true;
    }
    if(text == "minimum" || text == "min") {
        value = 0; return true;
    }

    const auto found = small.find(text);

    if(found != small.end()) {
        value = found->second / 100.0; return true;
    }

    const auto space = text.find(' ');

    if(space == string::npos) {
        return false;
    }

    const auto tens = small.find(text.substr(0, space));
    const auto units = small.find(text.substr(space + 1));

    const auto isValidCompoundNumber = [&] {
        return tens != small.end() && units != small.end() && tens->second >= 20 &&
            tens->second % 10 == 0 && units->second >= 1 && units->second <= 9;
    };

    if(!isValidCompoundNumber()) {
        return false;
    }

    value = (tens->second + units->second) / 100.0;
    return true;
}

struct Rule {
    regex pattern;
    const char* action;
    int device;
    int number;
    int mute; // -1: no mute parameter; 0: unmute; 1: mute
    Rule(const string& pattern, const char* action, int device = 0, int number = 0, int mute = -1)
        : pattern(pattern, regex::ECMAScript | regex::optimize),
          action(action), device(device), number(number), mute(mute) {}
};

const vector<Rule>& rules() {
    // Noncapturing groups keep device/value capture indexes explicit and stable.
    static const string volume = R"((?:volume|volum|volumn|volome))";
    static const string prep = R"((?:of|on|for|in))";
    static const string setter = R"((?:(?:set|change|adjust|turn|put) )?)";
    static const vector<Rule> compiled = {
        // set [the] volume of <device> to <level>; volume on <device> <level>
        {setter + "(?:the )?" + volume + " " + prep + R"( (.+?) (?:to|at) (.+))", "volume_set", 1, 2},
        {setter + "(?:the )?" + volume + " " + prep + R"( (.+) ([0-9]+(?:\.[0-9]+)?%?))", "volume_set", 1, 2},
        {setter + "(?:the )?" + volume + R"( (?:to |at )?(.+))", "volume_set", 0, 1},
        // set [the] <device>['s] volume to <level>
        {setter + R"((?:the )?(.+?) )" + volume + R"( (?:to |at )?(.+))", "volume_set", 1, 2},
        // set volume to <level> [on <device>]
        {setter + "(?:the )?" + volume + R"( (?:to |at )?(.+?) )" + prep + R"( (.+))", "volume_set", 2, 1},
        // turn <device> to <level>; set <device> to <level>
        {R"((?:set|turn|adjust) (.+?) (?:volume )?to (.+))", "volume_set", 1, 2},
        {R"((?:set|turn) (.+?) to (?:full|maximum|max) volume)", "volume_set", 1},
        // Match omitted targets before generic device captures ("volume" is not a device).
        {R"((?:turn (?:the )?volume up|volume up|turn up|louder)(?: by (.+))?)", "volume_up", 0, 1},
        {R"((?:turn (?:the )?volume down|volume down|turn down|quieter)(?: by (.+))?)", "volume_down", 0, 1},
        {R"((?:raise|increase|boost|turn up) (?:the )?volume(?: by (.+))?)", "volume_up", 0, 1},
        {R"((?:lower|decrease|reduce|turn down) (?:the )?volume(?: by (.+))?)", "volume_down", 0, 1},
        {R"(volume up (?:on |in |for )?(.+?)(?: by (.+))?)", "volume_up", 1, 2},
        {R"(volume down (?:on |in |for )?(.+?)(?: by (.+))?)", "volume_down", 1, 2},
        // Relative changes: raise volume on <device> [by <amount>].
        {R"((?:raise|increase|boost|turn up) (?:the )?(?:volume|volum)(?: )?)" + prep + R"( (.+?)(?: by (.+))?)", "volume_up", 1, 2},
        {R"((?:lower|decrease|reduce|turn down) (?:the )?(?:volume|volum)(?: )?)" + prep + R"( (.+?)(?: by (.+))?)", "volume_down", 1, 2},
        {R"((?:raise|increase|boost) (?:the )?(.+?) volume(?: by (.+))?)", "volume_up", 1, 2},
        {R"((?:lower|decrease|reduce) (?:the )?(.+?) volume(?: by (.+))?)", "volume_down", 1, 2},
        {R"(turn (?:the )?(?:volume )?up(?: )?(?:on |in |for )?(.+?)(?: by (.+))?)", "volume_up", 1, 2},
        {R"(turn (?:the )?(?:volume )?down(?: )?(?:on |in |for )?(.+?)(?: by (.+))?)", "volume_down", 1, 2},
        {R"(turn (.+?) volume up(?: by (.+))?)", "volume_up", 1, 2},
        {R"(turn (.+?) volume down(?: by (.+))?)", "volume_down", 1, 2},
        {R"(turn (.+?) up(?: by (.+))?)", "volume_up", 1, 2},
        {R"(turn (.+?) down(?: by (.+))?)", "volume_down", 1, 2},
        {R"((?:make )?(.+?) (?:louder)(?: by (.+))?)", "volume_up", 1, 2},
        {R"((?:make )?(.+?) (?:quieter|softer)(?: by (.+))?)", "volume_down", 1, 2},
        // Mute and playback accept arbitrary device names after optional prepositions.
        {R"((?:unmute|un mute)(?: (?:the )?(?:volume|audio|sound))?(?: (?:on |in |for )?(.+))?)", "volume_mute", 1, 0, 0},
        {R"((?:mute|silence)(?: (?:the )?(?:volume|audio|sound))?(?: (?:on |in |for )?(.+))?)", "volume_mute", 1, 0, 1},
        {R"((?:play|resume|unpause|continue)(?: (?:the )?(?:music|audio|playback))?(?: (?:on |in |for )?(.+))?)", "media_play", 1},
        {R"((?:pause|paus)(?: (?:the )?(?:music|audio|playback))?(?: (?:on |in |for )?(.+))?)", "media_pause", 1},
        {R"(stop(?: (?:the )?(?:music|audio|playback))?(?: (?:on |in |for )?(.+))?)", "media_stop", 1},
        {R"((?:toggle|play pause)(?: (?:the )?(?:music|audio|playback))?(?: (?:on |in |for )?(.+))?)", "media_play_pause", 1},
        {R"((?:skip to (?:the )?next(?: (?:song|track))?|next(?: (?:song|track))?|skip(?: (?:this|the))?(?: (?:song|track))?)(?: (?:on |in |for )?(.+))?)", "media_next_track", 1},
        {R"((?:previous(?: (?:song|track))?|go back(?: (?:a|one) (?:song|track))?)(?: (?:on |in |for )?(.+))?)", "media_previous_track", 1},
        {R"((?:turn|switch|power) on(?: (.+))?)", "turn_on", 1},
        {R"((?:turn|switch|power) off(?: (.+))?)", "turn_off", 1},
        {R"((?:turn|switch|power) (.+) on)", "turn_on", 1},
        {R"((?:turn|switch|power) (.+) off)", "turn_off", 1},
        {R"((.+?) (?:pause|paus))", "media_pause", 1},
        {R"((.+?) (?:play|resume))", "media_play", 1},
        {R"((.+?) unmute)", "volume_mute", 1, 0, 0},
        {R"((.+?) mute)", "volume_mute", 1, 0, 1}
    };

    return compiled;
}

} // namespace

string normalizeDeviceName(string name) {
    name = normalize(name);

    if(name.compare(0, 4, "the ") == 0) {
        name.erase(0, 4);
    }

    if(name.size() > 2 && name.substr(name.size() - 2) == "'s") {
        name.resize(name.size() - 2);
    }
    
    return trim(name);
}

json parse(const string& input, const Options& options) {
    // Bound regex work on untrusted speech input. Rules compile just once.
    if(input.empty() || input.size() > 512) {
        return nullptr;
    }

    static const regex negative(R"(-\s*(?:[0-9]))", regex::icase);

    if(std::regex_search(input, negative)) {
        return nullptr;
    }

    auto text = normalize(input);
    static const regex unsafe(R"(\b(?:not|never|dont|don't|and|then|but)\b)");

    if(std::regex_search(text, unsafe)) {
        return nullptr;
    }

    static const regex prefix(R"(^(?:(?:please|hey|okay|ok) |(?:can|could|would) you (?:please )?)+)");
    static const regex suffix(R"( please$)");
    text = std::regex_replace(text, prefix, "");
    text = std::regex_replace(text, suffix, "");

    for(const auto& rule : rules()) {
        smatch match;

        if(!std::regex_match(text, match, rule.pattern)) {
            continue;
        }

        double number = 1;
        const bool hasNumber = rule.number && match[rule.number].matched;

        if(hasNumber && !level(match[rule.number].str(), number)) {
            continue;
        }

        json target;

        if(!resolveDevice(rule.device ? match[rule.device].str() : "", options, target)) {
            continue;
        }

        if(hasNumber && string(rule.action) != "volume_set") {
            json body = {{"text", input}, {"language", "en"}};

            if(!options.conversationAgent.empty()) {
                body["agent_id"] = options.conversationAgent;
            }

            return {{"endpoint", "/conversation/process"}, {"body", body}};
        }

        if(target.is_null() && !options.defaultDevice.empty()) {
            target = options.defaultDevice;
        }

        const string action = rule.action;
        json body = {{"entity_id", target}};

        if(action == "volume_set") {
            body["volume_level"] = number;
        }

        if(rule.mute >= 0) {
            body["is_volume_muted"] = rule.mute == 1;
        }

        return {{"endpoint", "/services/media_player/" + action}, {"body", body}};
    }
    return nullptr;
}

} // namespace Parser