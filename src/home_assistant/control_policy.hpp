/**
 * @file
 * @brief Pure command validation and the AdvantageAir airflow policy.
 */

#pragma once

#include "commands/command.hpp"
#include <vector>

using namespace Commands;
using std::vector;

/**
 * @brief One ducted AdvantageAir system as verified on site (see docs/advantage_air.md).
 */
struct AirflowSystem {
    /// Main climate entity that runs the compressor.
    string entity;

    /// MyZone select entity, which voice must never change; empty when not configured.
    string myZoneEntity;

    /// Every zone's climate entity.
    vector<string> zones;

    /// Zones the installer requires open whenever the system runs.
    vector<string> constantZones;

    /// Fewest zones that may be open while the system runs.
    unsigned minimumOpenZones = 1;
    
    /// Preset under which the constant-zone policy was verified.
    string verifiedPreset;
};

/**
 * @brief Every verified system; climate and damper commands outside them are rejected.
 */
using AirflowPolicy = vector<AirflowSystem>;

/**
 * @brief Read the advantage_air section of a configuration.
 * @return Verified systems; empty when the section is absent.
 * @throws std::runtime_error If the configuration or section is malformed.
 */
AirflowPolicy readAirflowPolicy(const json& configuration);

/**
 * @brief Resolve and validate a command against a device snapshot and the airflow policy.
 *
 * Converts relative volume into volume_set from the speaker's current level, then checks the
 * target, the service's discovered fields and their selectors (numeric ranges, booleans), device
 * capabilities, temperature limits, select options, and AC airflow rules. Trusts nothing about
 * the parser that produced the command.
 *
 * @param[in] services Services commands may name, as discovered from Home Assistant.
 * @return Success with the resolved command, or the rejection reason. Never throws.
 */
Result validateCommand(Command command, const Devices& devices, const ServiceCatalogue& services, const AirflowPolicy& policy);