/**
 * @file
 * @brief Reading rooms, physical devices and aliases from the configuration.
 */

#include "registry.hpp"
#include "parser/speech_text.hpp"

#include <algorithm>
#include <stdexcept>

using Grammar::normalizeName;
using std::runtime_error;

namespace Home {
namespace {
/**
 * @brief A section of the configuration that must be an object, or an empty one when absent.
 */
json section(const json& configuration, const char* key, const char* shape) {
    auto value = configuration.value(key, json::object());

    if(!value.is_object()) {
        throw runtime_error(string(key) + " must map " + shape);
    }

    return value;
}

/**
 * @brief A configuration key as words, since keys are written with underscores.
 */
string keyWords(string key) {
    std::replace(key.begin(), key.end(), '_', ' ');
    return key;
}

/**
 * @brief Read a list of spoken names.
 * @param[in] owner What the names belong to, for the error message.
 */
vector<string> nameList(const json& names, const string& owner) {
    if(!names.is_array()) {
        throw runtime_error("Names for " + owner + " must be a list");
    }

    vector<string> result;

    for(const auto& name : names) {
        if(!name.is_string() || normalizeName(name.get<string>()).empty()) {
            throw runtime_error("Names for " + owner + " must be nonempty strings");
        }

        result.push_back(name.get<string>());
    }

    return result;
}

/**
 * @brief Read one entry of the devices section.
 */
PhysicalDevice readDevice(const string& name, const json& entry) {
    if(!entry.is_object()) {
        throw runtime_error("Device " + name + " needs its entities listed");
    }

    for(const auto& [key, value] : entry.items()) {
        if(key != "room" && key != "aliases" && key != "entities") {
            throw runtime_error("Device " + name + " has an unknown setting: " + key);
        }
    }

    PhysicalDevice device{keyWords(name), keyWords(entry.value("room", string())), nameList(entry.value("aliases", json::array()), name), {}};

    for(const auto& entity : entry.value("entities", json::array())) {
        if(!entity.is_string() || entity.get<string>().find('.') == string::npos) {
            throw runtime_error("Device " + name + " lists something that isn't an entity ID");
        }

        device.entities.push_back(entity.get<string>());
    }

    if(device.entities.empty()) {
        throw runtime_error("Device " + name + " lists no entities");
    }

    return device;
}
} // namespace

Registry::Registry(const json& configuration) {
    const auto aliases = section(configuration, "aliases", "entity IDs to lists of spoken names");
    const auto rooms = section(configuration, "rooms", "room names to lists of other names");
    const auto devices = section(configuration, "devices", "device names to their room and entities");

    for(const auto& [entity, names] : aliases.items()) {
        if(entity.find('.') == string::npos) {
            throw runtime_error("Alias entries need an entity ID: " + entity);
        }

        entityAliases[entity] = nameList(names, entity);
    }

    for(const auto& [name, others] : rooms.items()) {
        allRooms.push_back({keyWords(name), nameList(others, name)});
    }

    for(const auto& [name, entry] : devices.items()) {
        auto device = readDevice(name, entry);

        for(const auto& entity : device.entities) {
            if(!owners.emplace(entity, allDevices.size()).second) {
                throw runtime_error("Entity " + entity + " is listed under two devices, or twice");
            }
        }

        allDevices.push_back(std::move(device));
    }
}

const PhysicalDevice* Registry::deviceOf(const string& entity) const {
    const auto owner = owners.find(entity);
    return owner == owners.end() ? nullptr : &allDevices[owner->second];
}

const Room* Registry::room(const string& name) const {
    const auto found = std::find_if(allRooms.begin(), allRooms.end(), [&](const Room& room) { return normalizeName(room.name) == normalizeName(name); });
    return found == allRooms.end() ? nullptr : &*found;
}

vector<string> Registry::aliasesOf(const string& entity) const {
    const auto* device = deviceOf(entity);
    vector<string> names;

    for(const auto& member : device ? device->entities : vector<string>{entity}) {
        const auto own = entityAliases.find(member);

        if(own != entityAliases.end()) {
            names.insert(names.end(), own->second.begin(), own->second.end());
        }
    }

    if(device) {
        names.push_back(device->name);
        names.insert(names.end(), device->aliases.begin(), device->aliases.end());
    }

    return names;
}
} // namespace Home