/**
 * @file
 * @brief The service catalogue: which services commands may name, as discovered by an executor.
 */

#pragma once

#include <map>
#include <set>
#include <string>
#include <nlohmann/json.hpp>

namespace Commands {

using json = nlohmann::json;

/**
 * @brief The data fields one service accepts.
 */
struct ServiceFields {

    /// Every field the service accepts besides entity_id.
    std::set<std::string> allowed;

    /// Fields that must be present.
    std::set<std::string> required;

    /// Selector describing each field's values, such as {"number": {"min": 0, "max": 1}} or {"boolean": {}}.
    std::map<std::string, json> selectors;
};

/**
 * @brief Services by domain, then by service name.
 */
using ServiceCatalogue = std::map<std::string, std::map<std::string, ServiceFields>>;

/**
 * @brief Look up the fields of domain.service.
 * @return Catalogue entry, or nullptr when commands may not name that service.
 */
inline const ServiceFields* findService(const ServiceCatalogue& services, const std::string& domain, const std::string& service) {
    const auto group = services.find(domain);

    if(group == services.end()) {
        return nullptr;
    }

    const auto entry = group->second.find(service);
    return entry == group->second.end() ? nullptr : &entry->second;
}
} // namespace Commands