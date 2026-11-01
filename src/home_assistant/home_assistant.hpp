/**
 * @file
 * @brief Home Assistant executor: device discovery and checked service calls over its REST API.
 */

#pragma once

#include "control_policy.hpp"
#include <httplib.h>
#include <string>

using namespace Commands;

/**
 * @brief Extract controllable entities from a Home Assistant /api/states array.
 *
 * Keeps entities whose domain Commands::serviceCatalog() lists, skips malformed records, and
 * falls back to the object ID when an entity has no friendly name.
 *
 * @throws std::runtime_error If the states are not an array.
 */
Devices controllableDevices(const json& states);

/**
 * @brief Executes commands as Home Assistant service calls, guarded by the airflow policy.
 */
class HomeAssistant final : public Executor {

    /// Authenticated REST client.
    httplib::Client client;

    /// Verified AdvantageAir systems from the active configuration.
    AirflowPolicy policy;

    /**
     * @brief GET an API path that returns a JSON array.
     * @throws std::runtime_error For transport errors, non-200 statuses or non-array bodies.
     */
    json getArray(const std::string& path);

public:
    /**
     * @brief Configure the REST client without contacting the server.
     * @param[in] url Origin including scheme and optional port, such as "http://wyvern:8123".
     * @param[in] token Long-lived access token.
     */
    HomeAssistant(const std::string& url, const std::string& token);

    /**
     * @brief Replace the airflow policy with the configuration's advantage_air section.
     * @throws std::runtime_error If the section is malformed; the previous policy stays active.
     */
    void configure(const json& configuration) override;

    /**
     * @brief Fetch current states and return the controllable devices.
     */
    Devices discoverDevices() override;

    /**
     * @brief Validate against fresh state, the policy and advertised services, without sending.
     */
    Result preview(const Command& command) override;

    /**
     * @brief Preview the command, then POST it once. Never retries.
     */
    Result execute(const Command& command) override;
};