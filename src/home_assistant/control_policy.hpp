/**
 * @file
 * @brief Pure command resolution and installation-policy validation contracts.
 */

#pragma once

#include "commands/command.hpp"

/**
 * @brief Resolve internal intents and validate a request against supplied state and policy.
 *
 * Validates control-policy structure, resolves relative media volume using a finite
 * current level and a signed delta within -1..1, and clamps the resulting level to
 * 0..1. Then checks target availability, domain/service/field compatibility, device
 * capabilities, temperature limits/steps, selector options, and AC airflow rules.
 * Arbitrary conversation requests are rejected independently of parser behavior.
 *
 * @param[in] request Proposed service command or internal relative-volume intent.
 * @param[in] states Caller-supplied entity-state snapshot; no refresh occurs here.
 * @param[in] configuration Installation policy used to authorize climate/airflow changes.
 * @return Success with a resolved request, or failure with an explanatory error.
 *         Caught validation/data exceptions are converted to failure results.
 * @note Pure with respect to caller data and external systems: no HTTP, logging,
 *       mutation of input snapshots, or device control.
 */
Commands::ExecutionResult prepareCommand(const Commands::json& request, const Commands::json& states,
                                         const Commands::json& configuration);
/**
 * @brief Return only the rejection reason from prepareCommand().
 *
 * Resolves internal intents and performs the same snapshot-based validation, but
 * discards the resolved service request. Use prepareCommand() when that request
 * is needed for preview or execution.
 *
 * @param[in] request Proposed command or internal intent.
 * @param[in] states Explicit state snapshot to validate against.
 * @param[in] configuration Explicit installation/control policy snapshot.
 * @return Empty string when permitted, otherwise the validation failure explanation.
 * @note Performs no external operations and does not refresh stale state.
 */
std::string validateCommand(const Commands::json& request, const Commands::json& states,
                            const Commands::json& configuration);
/**
 * @brief Validate the structural consistency of configured AdvantageAir systems.
 *
 * Requires an object configuration. When advantage_air is present, checks its
 * systems array, main-entity uniqueness, nonempty unique zones, constant-zone
 * membership, verified-preset string, and a feasible minimum-open-zone count.
 * An absent AdvantageAir section is valid configuration; command checks later
 * reject climate/airflow actions without a matching verified system policy.
 *
 * @param[in] configuration Decoded application configuration; not modified.
 * @throws std::runtime_error If required policy fields or structural constraints fail.
 * @throws nlohmann::json::exception If a policy value cannot be converted as required.
 * @note Checks configuration only, not physical airflow, device existence, or live state.
 */
void validateControlConfiguration(const Commands::json& configuration);
