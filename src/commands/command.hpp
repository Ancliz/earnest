/**
 * @file
 * @brief Command vocabulary and the parser/executor contracts shared by every pipeline stage.
 */

#pragma once

#include "services.hpp"

#include <optional>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace Commands {
using std::string;

/**
 * @brief A controllable entity reported by an executor.
 */
struct Device {

    /// Full entity ID, such as "light.kitchen".
    string entityId;

    /// Friendly name, or the entity's object ID when it has none.
    string name;

    /// Entity domain, such as "light".
    string domain;

    /// Current state, such as "on" or "unavailable".
    string state;

    /// Device-reported capabilities and current values.
    json attributes = json::object();
};

/**
 * @brief Devices available to a parser or executor.
 */
using Devices = std::vector<Device>;

/**
 * @brief One service call: run domain.service on an entity with the given data fields.
 */
struct Command {

    /// Service domain, such as "media_player".
    string domain;

    /// Service name within the domain, such as "volume_set".
    string service;
    
    /// Target entity ID; empty when no device was named and no default applies.
    string entity;
    
    /// Service fields, excluding entity_id.
    json data = json::object();
};

/**
 * @brief Whether two commands have identical fields.
 */
inline bool operator==(const Command& left, const Command& right) {
    return std::tie(left.domain, left.service, left.entity, left.data) ==
           std::tie(right.domain, right.service, right.entity, right.data);
}

/**
 * @brief Whether two commands differ in any field.
 */
inline bool operator!=(const Command& left, const Command& right) {
    return !(left == right);
}

/**
 * @brief JSON conversion for printing commands and building them from model output.
 */
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(Command, domain, service, entity, data)

/**
 * @brief What a parser may refer to while interpreting text.
 */
struct Context {

    /// Devices discovered for this text.
    Devices devices;

    /// Services the devices offer, as discovered alongside them.
    ServiceCatalogue services;

    /// Entity used when the text names no device; ignored outside its own domain.
    string defaultDevice;

    /// Whether device names may match despite small misspellings.
    bool fuzzyNames = true;

    /**
     * @brief A context for the given devices and services with default preferences.
     */
    Context(Devices devices = {}, ServiceCatalogue services = {}) : devices(std::move(devices)), services(std::move(services)) {}
};

/**
 * @brief Outcome of previewing or executing a command.
 */
struct Result {
    
    /// Whether the command was accepted and, for execution, sent successfully.
    bool success = false;

    /// Reason for rejection or failure; empty on success.
    string error;

    /// The command as it would be or was sent, after internal resolution such as relative volume.
    Command command;

    /// Answer to a request for information, such as what is queued; empty when there is none.
    string reply = "";
};

/**
 * @brief Turns text into at most one command. Implementations never execute anything.
 */
class Parser {
public:
    /**
     * @brief Destroy a parser through the interface.
     */
    virtual ~Parser() = default;

    /**
     * @brief Replace the active configuration with a decoded phrase file.
     * @throws std::exception If the configuration is invalid; the previous one stays active.
     */
    virtual void configure(const json& configuration) = 0;

    /**
     * @brief Interpret completed speech or typed text.
     *
     * Non-const so that network-backed parsers can use their clients.
     *
     * @param[in] text Transcript or typed command.
     * @param[in] context Devices and name-resolution preferences for this text.
     * @return The single command the text asks for, or nullopt when unsupported or ambiguous.
     */
    virtual std::optional<Command> parse(const string& text, const Context& context) = 0;
};

/**
 * @brief Discovers devices and runs commands against them.
 */
class Executor {
public:
    /**
     * @brief Destroy an executor through the interface.
     */
    virtual ~Executor() = default;

    /**
     * @brief Replace the active configuration with a decoded phrase file.
     * @throws std::exception If the configuration is invalid; the previous one stays active.
     */
    virtual void configure(const json& configuration) = 0;

    /**
     * @brief Devices that commands may target right now.
     * @throws std::exception If the devices cannot be queried.
     */
    virtual Devices discoverDevices() = 0;

    /**
     * @brief Services that commands may name right now, with their fields.
     *
     * Executors may cache this, as services change far less often than device states.
     * @throws std::exception If the services cannot be queried.
     */
    virtual ServiceCatalogue discoverServices() = 0;

    /**
     * @brief Validate against live state and return what execute() would send, without sending it.
     * @return Failures as results; never throws.
     */
    virtual Result preview(const Command& command) = 0;

    /**
     * @brief Validate against live state, then send the command once.
     * @return Failures as results; never throws.
     */
    virtual Result execute(const Command& command) = 0;
};
} // namespace Commands