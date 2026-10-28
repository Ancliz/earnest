/**
 * @file
 * @brief Canonical aliases and bounded domain-scoped device resolution.
 */

#include "detail.hpp"

#include <algorithm>
#include <limits>
#include <regex>
#include <vector>

namespace Parser {
namespace Detail {
using std::numeric_limits;
using std::regex;
using std::size_t;
using std::vector;

/**
 * @brief Compute edit distance with early rejection beyond a small matching threshold.
 *
 * Uses two dynamic-programming rows for insertion, deletion, and substitution.
 * Rejects immediately when lengths differ too much or a completed row cannot
 * reach the permitted threshold.
 *
 * @param[in] a First normalized alias string.
 * @param[in] b Second normalized alias string.
 * @param[in] limit Largest distance relevant to the caller's fuzzy-match decision.
 * @return Exact distance when within limit; any value above limit means rejection.
 * @note Allocates local rows only and does not modify either string or alias context.
 */
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
            current[j] =
                std::min({previous[j] + 1, current[j - 1] + 1, previous[j - 1] + (a[i - 1] != b[j - 1])});
            best = std::min(best, current[j]);
        }

        if(best > limit) {
            return limit + 1;
        }

        previous.swap(current);
    }

    return previous[b.size()];
}

bool resolveDevice(const string& captured, const string& domain, const Options& options, json& target) {
    const auto name = normalizeDeviceName(captured);
    target = nullptr;

    if(name.empty()) {
        const bool sameDomain = options.defaultDevice.compare(0, domain.size() + 1, domain + ".") == 0;
        const bool unresolvedMediaName = domain == "media_player" && !options.strictDevices &&
                                         options.defaultDevice.find('.') == string::npos;

        if(!options.defaultDevice.empty() && (sameDomain || unresolvedMediaName)) {
            target = options.defaultDevice;
        }

        return domain == "media_player" || !target.is_null();
    }

    static const regex compound(R"(\b(then|but|to|by)\b)");

    if(std::regex_search(name, compound) || name.size() > 160) {
        return false;
    }

    const auto scoped = options.domainDevices.find(domain);
    const auto* aliases = scoped == options.domainDevices.end() ? &options.devices : &scoped->second;

    if(scoped == options.domainDevices.end() && (options.strictDevices || domain != "media_player")) {
        return false;
    }

    if(aliases->empty()) {
        if(options.strictDevices || name.find(" and ") != string::npos) {
            return false;
        }

        target = name;
        return true;
    }

    size_t best = numeric_limits<size_t>::max();
    string match;
    bool ambiguous = false;

    for(const auto& entry : *aliases) {
        const auto alias = normalizeDeviceName(entry.first);
        const auto limit = std::min(name.size(), alias.size()) >= 8 ? 2u : 1u;
        const auto edits = alias == name                              ? 0
                           : options.fuzzyDevices && name.size() >= 4 ? distance(name, alias, limit)
                                                                      : limit + 1;

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

} // namespace Detail
} // namespace Parser

namespace Commands {
using ::Parser::Detail::normalize;
using ::Parser::Detail::resolveDevice;
using ::Parser::Detail::trim;
using std::string;

/**
 * @copydoc Commands::normalizeDeviceName(std::string)
 */
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

/**
 * @brief Resolve a domain-scoped device name using the shared alias-matching rules.
 *
 * Delegates to the same resolver used by regex parsing, including default-target,
 * strict-discovery, bounded fuzzy-matching, and ambiguity behavior.
 *
 * @param[in] name Captured spoken target; empty input requests default handling.
 * @param[in] domain Requested Home Assistant entity domain.
 * @param[in] options Supplied alias/state context and matching preferences.
 * @return Resolved or permitted unresolved string, or JSON null when no target can be supplied.
 * @note Performs no discovery, file access, logging, or command execution.
 */
json resolveEntityId(const string& name, const string& domain, const Context& options) {
    json target;
    return resolveDevice(name, domain, options, target) ? target : json(nullptr);
}

} // namespace Commands
