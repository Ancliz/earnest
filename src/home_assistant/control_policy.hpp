/**
 * @file
 * @brief Pure command validation and the AdvantageAir airflow policy.
 */

#pragma once

#include "commands/command.hpp"
#include <vector>

using namespace Commands;

/**
 * @brief One ducted AdvantageAir system as verified on site (see docs/advantage_air.md).
 */
struct AirflowSystem {
    /// Main climate entity that runs the compressor.
    std::string entity;

    /// MyZone select entity, which voice must never change; empty when not configured.
    std::string myZoneEntity;

    /// Every zone's climate entity.
    std::vector<std::string> zones;

    /// Zones the installer requires open whenever the system runs.
    std::vector<std::string> constantZones;

    /// Fewest zones that may be open while the system runs.
    unsigned minimumOpenZones = 1;
    
    /// Preset under which the constant-zone policy was verified.
    std::string verifiedPreset;
};

/**
 * @brief Every verified system; climate and damper commands outside them are rejected.
 */
using AirflowPolicy = std::vector<AirflowSystem>;

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
 * target, catalog fields, numeric ranges, device capabilities, temperature limits, selector
 * options, and AC airflow rules. Trusts nothing about the parser that produced the command.
 *
 * @return Success with the resolved command, or the rejection reason. Never throws.
 */
Result validateCommand(Command command, const Devices& devices, const AirflowPolicy& policy);