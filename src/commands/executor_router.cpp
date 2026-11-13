/**
 * @file
 * @brief Domain routing across executors.
 */

#include "executor_router.hpp"
#include "log/log.hpp"

#include <spdlog/spdlog.h>

void ExecutorRouter::configure(const json& configuration) {
    for(auto* executor : executors) {
        executor->configure(configuration);
    }
}

Devices ExecutorRouter::discoverDevices() {
    Devices devices;

    for(auto* executor : executors) {
        try {
            const auto found = executor->discoverDevices();
            devices.insert(devices.end(), found.begin(), found.end());
        } catch(const std::exception& error) {
            Log::act()->warn("Skipping unreachable devices: {}", error.what());
        }
    }

    return devices;
}

ServiceCatalogue ExecutorRouter::discoverServices() {
    ServiceCatalogue services;
    owners.clear();

    for(auto* executor : executors) {
        try {
            for(auto& [domain, domainServices] : executor->discoverServices()) {
                // The first executor offering a domain owns it, so two never answer one command.
                if(owners.emplace(domain, executor).second) {
                    services[domain] = std::move(domainServices);
                }
            }
        } catch(const std::exception& error) {
            Log::act()->warn("Skipping unreachable services: {}", error.what());
        }
    }

    return services;
}

Executor* ExecutorRouter::route(const Command& command) {
    if(!owners.count(command.domain)) {
        discoverServices();
    }

    const auto owner = owners.find(command.domain);
    return owner == owners.end() ? nullptr : owner->second;
}

Result ExecutorRouter::preview(const Command& command) {
    auto* executor = route(command);
    return executor ? executor->preview(command) : Result{false, "No executor offers domain " + command.domain, command};
}

Result ExecutorRouter::execute(const Command& command) {
    auto* executor = route(command);
    return executor ? executor->execute(command) : Result{false, "No executor offers domain " + command.domain, command};
}