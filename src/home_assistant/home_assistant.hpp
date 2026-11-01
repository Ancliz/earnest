/**
 * @file
 * @brief Home Assistant executor: device discovery and checked service calls over its REST API.
 */

#pragma once

#include "control_policy.hpp"
#include <chrono>
#include <httplib.h>
#include <set>
#include <string>

using namespace Commands;

/**
 * @brief Domains and services voice may control, adjusted by the phrase file's voice_control section.
 */
struct VoiceScope {

    /// Excluded domains ("lock") and services ("media_player.join").
    std::set<std::string> excluded;

    /**
     * @brief Whether commands may name domain.service.
     */
    bool allows(const std::string& domain, const std::string& service) const;
};

/**
 * @brief Read voice_control from a configuration on top of the built-in exclusions.
 *
 * Security, system, notification and helper domains (locks, alarms, cameras, updates, scripts,
 * automations, ...) are excluded by default. voice_control.exclude adds domains or
 * domain.service names, and voice_control.allow removes default exclusions.
 *
 * @throws std::runtime_error If the section is malformed.
 */
VoiceScope readVoiceScope(const json& configuration);

/**
 * @brief Build the service catalogue from a Home Assistant /api/services array.
 *
 * Keeps services that target entities and that the scope allows. Fields come from each service's
 * field list, including fields grouped into collapsible sections, with their required flags and
 * selectors. volume_up and volume_down also accept an amount (0..1) that validation resolves into
 * volume_set.
 *
 * @throws std::runtime_error If the services are not an array.
 */
ServiceCatalogue readServiceCatalogue(const json& services, const VoiceScope& scope);

/**
 * @brief Extract controllable entities from a Home Assistant /api/states array.
 *
 * Keeps entities whose domain offers at least one catalogued service, skips malformed records, and
 * falls back to the object ID when an entity has no friendly name.
 *
 * @throws std::runtime_error If the states are not an array.
 */
Devices controllableDevices(const json& states, const ServiceCatalogue& services);

/**
 * @brief Executes commands as Home Assistant service calls, guarded by the airflow policy.
 *
 * Devices are discovered fresh for every command, as validation needs live state. Services change
 * rarely, so the catalogue is cached for a minute, refetched early when a command names a service
 * it lacks, and cleared whenever the configuration changes.
 */
class HomeAssistant final : public Executor {

    /// Monotonic clock for the catalogue's age.
    using Clock = std::chrono::steady_clock;

    /// How long a fetched catalogue is used before it is fetched again.
    static constexpr auto catalogueLifetime = std::chrono::seconds(60);

    /// Shortest gap between early refetches for unknown services, so bad commands cannot flood the server.
    static constexpr auto refetchInterval = std::chrono::seconds(5);

    /// Authenticated REST client.
    httplib::Client client;

    /// Verified AdvantageAir systems from the active configuration.
    AirflowPolicy policy;

    /// Domains and services voice may control.
    VoiceScope scope;

    /// Cached service catalogue.
    ServiceCatalogue services;

    /// When services was fetched; default-constructed (the epoch) when it must be fetched.
    Clock::time_point fetched;

    /**
     * @brief GET an API path that returns a JSON array.
     * @throws std::runtime_error For transport errors, non-200 statuses or non-array bodies.
     */
    json getArray(const std::string& path);

    /**
     * @brief Fetch the catalogue when it is older than its lifetime, or than the refetch interval if forced.
     * @return Whether it was fetched.
     */
    bool refreshServices(bool force = false);

public:
    /**
     * @brief Configure the REST client without contacting the server.
     * @param[in] url Origin including scheme and optional port, such as "http://homeassistant.local:8123".
     * @param[in] token Long-lived access token.
     */
    HomeAssistant(const std::string& url, const std::string& token);

    /**
     * @brief Apply the advantage_air and voice_control sections, and refetch services on next use.
     * @throws std::runtime_error If a section is malformed; the previous configuration stays active.
     */
    void configure(const json& configuration) override;

    /**
     * @brief Fetch current states and return the controllable devices.
     */
    Devices discoverDevices() override;

    /**
     * @brief The cached service catalogue, refreshed once it is a minute old.
     */
    ServiceCatalogue discoverServices() override;

    /**
     * @brief Validate against fresh state, the policy and the service catalogue, without sending.
     */
    Result preview(const Command& command) override;

    /**
     * @brief Preview the command, then POST it once. Never retries.
     */
    Result execute(const Command& command) override;
};