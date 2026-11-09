/**
 * @file
 * @brief The house as configured: rooms, and the entities that are really one device.
 */

#pragma once

#include "commands/command.hpp"

#include <map>
#include <vector>

namespace Home {
using Commands::json;
using std::string;
using std::vector;

/**
 * @brief A room, as Home Assistant or a device entry names it, and the other names it goes by.
 */
struct Room {

    /// Name the room is configured under.
    string name;

    /// Other spoken names.
    vector<string> aliases;
};

/**
 * @brief One physical device, which executors may each know as a separate entity.
 */
struct PhysicalDevice {

    /// Key the device is configured under, as words; it also counts as a spoken name.
    string name;

    /// Room it is in, overriding the one discovered; empty when not configured.
    string room;

    /// Other spoken names.
    vector<string> aliases;

    /// Its entity IDs, in order of preference within a domain.
    vector<string> entities;
};

/**
 * @brief Corrections to what discovery says: rooms, physical devices and spoken names.
 *
 * All optional. Entities not listed under a device are grouped by what the executors report.
 */
class Registry {

    /// Configured rooms.
    vector<Room> allRooms;

    /// Configured physical devices.
    vector<PhysicalDevice> allDevices;

    /// Extra spoken names by entity ID.
    std::map<string, vector<string>> entityAliases;

    /// Index into allDevices by entity ID.
    std::map<string, std::size_t> owners;

public:
    /**
     * @brief A registry with nothing configured.
     */
    Registry() = default;

    /**
     * @brief Read the configuration's aliases, rooms and devices.
     * @throws std::runtime_error If any of them is malformed.
     */
    explicit Registry(const json& configuration);

    /**
     * @brief Every configured physical device.
     */
    const vector<PhysicalDevice>& devices() const { return allDevices; }

    /**
     * @brief The configured device an entity belongs to, or nullptr.
     */
    const PhysicalDevice* deviceOf(const string& entity) const;

    /**
     * @brief The configured room of this name, however it is capitalised, or nullptr.
     */
    const Room* room(const string& name) const;

    /**
     * @brief Spoken names configured for an entity or for anything else in its physical device.
     */
    vector<string> aliasesOf(const string& entity) const;
};
} // namespace Home