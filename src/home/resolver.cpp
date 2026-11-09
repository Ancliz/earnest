/**
 * @file
 * @brief Ranking devices against spoken names.
 */

#include "resolver.hpp"
#include "parser/speech_text.hpp"

#include <algorithm>
#include <iterator>
#include <limits>
#include <unordered_map>

using Commands::Context;
using Commands::Device;
using Grammar::editDistance;
using Grammar::nameWords;
using Grammar::normalizeName;
using std::size_t;

namespace Home {
namespace {
/**
 * @brief Whether a heard word could be a misheard or shortened name word.
 *
 * Short words must match exactly. Longer ones may differ by about a third, capped at three
 * edits, so "ramp" matches "lamp" and model number "q919" matches "q990b" but "queue" misses "q800b"; a
 * three-letter start also matches, so "bedroom" matches "bed".
 */
bool soundsLike(const string& heard, const string& name, bool allowEdits) {
    if(heard == name) {
        return true;
    } else if(heard.size() < 3 || name.size() < 3) {
        return false;
    } else if(heard.compare(0, name.size(), name) == 0 || name.compare(0, heard.size(), heard) == 0) {
        return true;
    } else if(!allowEdits) {
        return false;
    }

    const auto limit = std::min<size_t>(3, (std::max(heard.size(), name.size()) + 1) / 2);
    return editDistance(heard, name, limit) <= limit;
}

/**
 * @brief How many heard words match one of a device's words.
 * @param[in] allowEdits Whether a misheard word counts, rather than only a whole or partial word.
 */
size_t wordScore(const vector<string>& heard, const vector<string>& names, bool allowEdits) {
    static const std::set<string> fillers = {"the", "and", "of", "to", "in", "my"};
    size_t score = 0;

    for(const auto& word : heard) {
        const auto matches = [&](const string& name) { return soundsLike(word, name, allowEdits); };

        if(!fillers.count(word) && std::any_of(names.begin(), names.end(), matches)) {
            ++score;
        }
    }

    return score;
}

/**
 * @brief The discovered entities with this one's name in a domain.
 */
vector<const Device*> sameNamed(const vector<const Device*>& devices, const Device& device, const string& domain) {
    vector<const Device*> matches;
    std::copy_if(devices.begin(), devices.end(), std::back_inserter(matches), [&](const Device* other) { return other->domain == domain && normalizeName(other->name) == normalizeName(device.name); });
    return matches;
}

/**
 * @brief Group discovered entities into physical devices, from what their executors report.
 *
 * Hardware with several entities in one domain is left apart: a ducted AC is one device to Home
 * Assistant, but each zone must keep its own name. A name link needs one entity on each side.
 *
 * @return One list of entities per physical device; some may be empty.
 */
vector<vector<const Device*>> groupDiscovered(const vector<const Device*>& devices) {
    std::map<std::pair<string, string>, size_t> perDomain;
    std::set<string> shared;

    for(const auto* device : devices) {
        if(!device->hardwareId.empty() && ++perDomain[{device->hardwareId, device->domain}] > 1) {
            shared.insert(device->hardwareId);
        }
    }

    vector<vector<const Device*>> groups;
    std::unordered_map<string, size_t> hardware;

    for(const auto* device : devices) {
        if(device->hardwareId.empty() || shared.count(device->hardwareId)) {
            groups.push_back({device});
        } else {
            const auto [slot, added] = hardware.emplace(device->hardwareId, groups.size());

            if(added) {
                groups.emplace_back();
            }

            groups[slot->second].push_back(device);
        }
    }

    const auto holding = [&](const Device* device) -> vector<const Device*>& {
        return *std::find_if(groups.begin(), groups.end(), [&](const auto& group) { return std::find(group.begin(), group.end(), device) != group.end(); });
    };

    for(const auto* device : devices) {
        const auto twins = device->sameNameDomain.empty() ? vector<const Device*>() : sameNamed(devices, *device, device->sameNameDomain);

        if(twins.size() != 1 || sameNamed(devices, *device, device->domain).size() != 1) {
            continue;
        }

        auto& from = holding(device);
        auto& into = holding(twins[0]);

        if(&from != &into) {
            into.insert(into.end(), from.begin(), from.end());
            from.clear();
        }
    }

    return groups;
}
} // namespace

void Resolver::Known::addName(const string& name) {
    const auto normalized = normalizeName(name);

    if(!normalized.empty()) {
        names.insert(normalized);
        const auto spoken = nameWords(name);
        words.insert(words.end(), spoken.begin(), spoken.end());
    }
}

void Resolver::Known::addEntity(const Device& device, const Registry& registry) {
    string objectName = device.entityId.substr(device.entityId.find('.') + 1);
    std::replace(objectName.begin(), objectName.end(), '_', ' ');
    entities[device.domain].push_back(device.entityId);
    addName(device.name);
    addName(objectName);

    // The full ID can be said to a parser but its domain's words mustn't count as name words.
    names.insert(normalizeName(device.entityId));

    for(const auto& alias : registry.aliasesOf(device.entityId)) {
        addName(alias);
    }
}

void Resolver::addDevice(const vector<const Device*>& members, const string& configuredRoom, const Registry& registry) {
    Known device;
    device.room = normalizeName(configuredRoom);

    for(const auto* member : members) {
        device.addEntity(*member, registry);

        if(device.room.empty()) {
            device.room = normalizeName(member->room);
        }
    }

    // Words only: the room narrows "rumpus speaker" down, but the room's name isn't the device's.
    auto roomWords = nameWords(device.room);

    if(const auto* place = registry.room(device.room)) {
        for(const auto& alias : place->aliases) {
            const auto spoken = nameWords(alias);
            roomWords.insert(roomWords.end(), spoken.begin(), spoken.end());
        }
    }

    device.words.insert(device.words.end(), roomWords.begin(), roomWords.end());
    known.push_back(std::move(device));
}

Resolver::Resolver(const Registry& registry, const Context& context)
    : defaultDevice(context.defaultDevice), room(normalizeName(context.room)), fuzzy(context.fuzzyNames) {
    std::unordered_map<string, const Device*> discovered;

    for(const auto& device : context.devices) {
        discovered.emplace(device.entityId, &device);
    }

    for(const auto& physical : registry.devices()) {
        vector<const Device*> members;

        for(const auto& entity : physical.entities) {
            const auto found = discovered.find(entity);

            if(found != discovered.end()) {
                members.push_back(found->second);
                discovered.erase(found);
            }
        }

        if(!members.empty()) {
            addDevice(members, physical.room, registry);
        }
    }

    vector<const Device*> unclaimed;

    for(const auto& device : context.devices) {
        // Erasing means an entity reported twice still counts once.
        if(discovered.erase(device.entityId)) {
            unclaimed.push_back(&device);
        }
    }

    for(const auto& members : groupDiscovered(unclaimed)) {
        if(!members.empty()) {
            addDevice(members, "", registry);
        }
    }
}

string Resolver::defaultIn(const string& domain) const {
    if(defaultDevice.compare(0, domain.size() + 1, domain + ".") == 0) {
        return defaultDevice;
    }

    // A cast targets a Spotify entity, but "turn it up" needs the same speaker's media player.
    for(const auto& device : known) {
        const auto inDomain = device.entities.find(domain);
        const auto holds = [&](const auto& entry) { return std::find(entry.second.begin(), entry.second.end(), defaultDevice) != entry.second.end(); };

        if(inDomain != device.entities.end() && std::any_of(device.entities.begin(), device.entities.end(), holds)) {
            return inDomain->second.front();
        }
    }

    return "";
}

vector<const Resolver::Known*> Resolver::closestNames(const string& name, const string& domain, bool allowEdits) const {
    const auto none = std::numeric_limits<size_t>::max();
    size_t bestEdits = none;
    vector<const Known*> closest;

    for(const auto& device : known) {
        if(!device.entities.count(domain)) {
            continue;
        }

        size_t edits = none;

        // Each name has an edit budget: 1, or 2 for names of 8+ characters.
        for(const auto& candidate : device.names) {
            const size_t limit = std::min(name.size(), candidate.size()) >= 8 ? 2 : 1;
            const size_t distance = candidate == name ? 0 : allowEdits && name.size() >= 4 ? editDistance(name, candidate, limit) : limit + 1;

            if(distance <= limit) {
                edits = std::min(edits, distance);
            }
        }

        if(edits < bestEdits) {
            closest = {&device};
            bestEdits = edits;
        } else if(edits != none && edits == bestEdits) {
            closest.push_back(&device);
        }
    }

    return closest;
}

Resolution Resolver::matchWords(const vector<string>& heard, const string& domain, const string& preferred) const {
    vector<const Known*> best;
    size_t bestScore = 0;
    size_t closeScore = 0;
    size_t otherScore = 0;
    size_t otherCloseScore = 0;

    for(const auto& device : known) {
        const auto score = wordScore(heard, device.words, fuzzy);
        const auto close = wordScore(heard, device.words, false);

        if(!device.entities.count(domain)) {
            otherScore = std::max(otherScore, score);
            otherCloseScore = std::max(otherCloseScore, close);
            continue;
        }

        closeScore = std::max(closeScore, close);

        if(score > bestScore) {
            best = {&device};
            bestScore = score;
        } else if(score == bestScore) {
            best.push_back(&device);
        }
    }

    if(bestScore == 0) {
        return {};
    } else if(otherScore > bestScore || otherCloseScore > closeScore) {
        // A device outside the domain matches better, so the domain is what's wrong.
        return {Resolution::Outcome::OtherDomain, ""};
    }

    return choose(std::move(best), domain, preferred);
}

Resolution Resolver::choose(vector<const Known*> matches, const string& domain, const string& preferred) const {
    vector<const Known*> here;
    std::copy_if(matches.begin(), matches.end(), std::back_inserter(here), [&](const Known* device) { return !room.empty() && device->room == room; });

    if(!here.empty()) {
        matches = std::move(here);
    }

    for(const auto* device : matches) {
        const auto& entities = device->entities.at(domain);

        if(std::find(entities.begin(), entities.end(), preferred) != entities.end()) {
            return {Resolution::Outcome::Found, preferred};
        }
    }

    return matches.size() > 1 ? Resolution{Resolution::Outcome::Ambiguous, ""} : Resolution{Resolution::Outcome::Found, matches[0]->entities.at(domain).front()};
}

Resolution Resolver::resolve(const string& spoken, const string& domain, Reach reach, const string& preferred) const {
    const auto name = normalizeName(spoken);

    if(name.empty()) {
        return {};
    }

    // Edits are only for whole names here: Words has its own tolerance, which watches other domains.
    auto named = closestNames(name, domain, reach == Reach::Names && fuzzy);

    if(!named.empty()) {
        return choose(std::move(named), domain, preferred);
    }

    return reach == Reach::Words ? matchWords(nameWords(name), domain, preferred) : Resolution{};
}
} // namespace Home