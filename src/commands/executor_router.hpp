/**
 * @file
 * @brief One executor that routes each command to whichever executor offers its domain.
 */

#pragma once

#include "commands/command.hpp"

#include <map>
#include <utility>
#include <vector>

using Commands::Command;
using Commands::Devices;
using Commands::Executor;
using Commands::json;
using Commands::Result;
using Commands::ServiceCatalogue;
using Commands::string;

/**
 * @brief Combines several executors, so parsers see one set of devices and services.
 *
 * Each domain belongs to the first executor that offers it, so an executor listed earlier wins a
 * clash. An executor that cannot be reached is left out of discovery rather than failing it.
 */
class ExecutorRouter final : public Executor {

    /// Executors in priority order; not owned.
    std::vector<Executor*> executors;

    /// Executor that owns each domain, from the last service discovery.
    std::map<string, Executor*> owners;

    /**
     * @brief The executor that owns a command's domain, discovering services once if it is unknown.
     */
    Executor* route(const Command& command);

public:
    /**
     * @brief Route between the given executors, earlier ones taking precedence.
     */
    explicit ExecutorRouter(std::vector<Executor*> executors) : executors(std::move(executors)) {}

    /**
     * @brief Configure every executor.
     */
    void configure(const json& configuration) override;

    /**
     * @brief Every reachable executor's devices.
     */
    Devices discoverDevices() override;

    /**
     * @brief Every reachable executor's services, each domain from the first executor offering it.
     */
    ServiceCatalogue discoverServices() override;

    /**
     * @brief Preview with the executor that owns the command's domain.
     */
    Result preview(const Command& command) override;

    /**
     * @brief Execute with the executor that owns the command's domain.
     */
    Result execute(const Command& command) override;
};