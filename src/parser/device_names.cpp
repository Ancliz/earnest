/**
 * @file
 * @brief Spoken device-name lookup with alias collisions and bounded fuzzy matching.
 */

#include "device_names.hpp"
#include "speech_text.hpp"

#include <algorithm>
#include <limits>
#include <regex>
#include <vector>

namespace Grammar {
using std::min;
using std::nullopt;
using std::optional;
using std::size_t;

size_t editDistance(const string& a, const string& b, size_t limit) {
    if(a.size() > b.size() + limit || b.size() > a.size() + limit) {
        return limit + 1;
    }

    std::vector<size_t> previous(b.size() + 1), current(b.size() + 1);

    for(size_t j = 0; j <= b.size(); ++j) {
        previous[j] = j;
    }

    // Two-row dynamic programming over insertions, deletions and substitutions.
    for(size_t i = 1; i <= a.size(); ++i) {
        current[0] = i;
        size_t rowBest = i;

        for(size_t j = 1; j <= b.size(); ++j) {
            current[j] =
                min({previous[j] + 1, current[j - 1] + 1, previous[j - 1] + (a[i - 1] != b[j - 1])});
            rowBest = min(rowBest, current[j]);
        }

        if(rowBest > limit) {
            return limit + 1;
        }

        previous.swap(current);
    }

    return previous[b.size()];
}

DeviceNames::DeviceNames(const Commands::Context& context, const json& aliases)
    : defaultDevice(context.defaultDevice), fuzzy(context.fuzzyNames) {
    for(const auto& device : context.devices) {
        string objectName = device.entityId.substr(device.entityId.find('.') + 1);
        std::replace(objectName.begin(), objectName.end(), '_', ' ');

        for(const string& name : {device.entityId, objectName, device.name}) {
            add(device.domain, name, device.entityId);
        }

        if(aliases.contains(device.entityId)) {
            for(const auto& alias : aliases.at(device.entityId)) {
                add(device.domain, alias.get<string>(), device.entityId);
            }
        }
    }
}

void DeviceNames::add(const string& domain, const string& name, const string& entity) {
    const string normalized = normalizeName(name);

    if(normalized.empty()) {
        return;
    }

    const auto [existing, inserted] = domains[domain].emplace(normalized, entity);

    if(!inserted && existing->second != entity) {
        existing->second.clear();
    }
}

optional<string> DeviceNames::resolve(const string& spoken, const string& domain) const {
    const string name = normalizeName(spoken);

    if(name.empty()) {
        if(defaultDevice.compare(0, domain.size() + 1, domain + ".") == 0) {
            return defaultDevice;
        }

        return domain == "media_player" ? optional<string>("") : nullopt;
    }

    // Joining words suggest the capture spans more than one device or command.
    static const std::regex compound(R"(\b(then|but|to|by)\b)");
    const auto names = domains.find(domain);

    if(std::regex_search(name, compound) || name.size() > 160 || names == domains.end()) {
        return nullopt;
    }

    size_t bestEdits = std::numeric_limits<size_t>::max();
    string bestEntity;
    bool ambiguous = false;

    // The closest name within the edit budget (1, or 2 for names of 8+ characters) wins; a tie
    // between different entities is ambiguous.
    for(const auto& [alias, entity] : names->second) {
        const size_t limit = min(name.size(), alias.size()) >= 8 ? 2 : 1;
        const size_t edits = alias == name               ? 0
                             : fuzzy && name.size() >= 4 ? editDistance(name, alias, limit)
                                                         : limit + 1;

        if(edits > limit) {
            continue;
        }

        if(edits < bestEdits) {
            bestEdits = edits;
            bestEntity = entity;
            ambiguous = false;
        } else if(edits == bestEdits && entity != bestEntity) {
            ambiguous = true;
        }
    }

    // An empty entity covers both "no match" and a blocked name.
    if(ambiguous || bestEntity.empty()) {
        return nullopt;
    }

    return bestEntity;
}
} // namespace Grammar