/**
 * @file
 * @brief Controllable-entity extraction and immutable-context construction.
 */

#pragma once

#include "commands/command.hpp"
#include <vector>

struct Device {
    std::string entityId;
    std::string friendlyName;
    std::string domain;
    std::string state;
    Commands::json attributes;
};

/**
 * @brief Extract controllable entity records from a supplied Home Assistant state array.
 *
 * Supports media_player, climate, light, switch, fan, cover, and select domains.
 * Skips malformed/unsupported entity IDs, normalizes missing/non-object attributes
 * to an empty object, and falls back from invalid friendly names to the entity name.
 * This operates on controllable entities, not physical-device registry entries.
 *
 * @param[in] states State array to inspect without modification.
 * @param[in] includeUnavailable Whether to retain explicit unknown/unavailable states.
 *                               Entries without a state field remain useful as fixtures.
 * @return Filtered device values in input order; an empty list is permitted.
 * @throws std::runtime_error If states is not an array.
 * @throws nlohmann::json::exception If a relevant state field has an incompatible type.
 * @note Does not fetch states or modify caller-owned data.
 */
std::vector<Device> findAvailableDevices(const Commands::json& states, bool includeUnavailable = false);
/**
 * @brief Build a fresh strict device-resolution context from supplied state records.
 *
 * Constructs domain-scoped aliases from friendly names, full IDs, and readable
 * entity names. Collisions between different entities are stored as blocked empty
 * IDs; repeated aliases for the same entity remain usable. Replaces discovery maps
 * and state snapshots in the returned copy while preserving matching preferences
 * and the configured default device.
 *
 * @param[in] states Home Assistant state array, including unavailable entities.
 * @param[in] options Base preferences, passed by value so the caller remains unchanged.
 * @return Updated context with strictDevices enabled and stale discovery aliases removed.
 * @throws std::runtime_error If states is not an array or contains no supported entities.
 * @throws nlohmann::json::exception If relevant state values have incompatible types.
 * @note Config-file aliases are overlaid later by the parser. No HTTP or logging occurs.
 */
Commands::Context buildCommandContext(const Commands::json& states, Commands::Context options = {});