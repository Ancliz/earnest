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
using std::map;
using std::set;
using std::string;

/**
 * @brief The data fields one service accepts.
 */
struct ServiceFields {

    /// Every field the service accepts besides entity_id.
    set<string> allowed;

    /// Fields that must be present.
    set<string> required;

    /// Selector describing each field's values, such as {"number": {"min": 0, "max": 1}} or {"boolean": {}}.
    map<string, json> selectors;

    /// Whether the service acts on a device; false for requests such as reading a music queue.
    bool targetsEntity = true;
};

/**
 * @brief Services by domain, then by service name.
 */
using ServiceCatalogue = map<string, map<string, ServiceFields>>;

/**
 * @brief Look up the fields of domain.service.
 * @return Catalogue entry, or nullptr when commands may not name that service.
 */
inline const ServiceFields* findService(const ServiceCatalogue& services, const string& domain, const string& service) {
    const auto group = services.find(domain);

    if(group == services.end()) {
        return nullptr;
    }

    const auto entry = group->second.find(service);
    return entry == group->second.end() ? nullptr : &entry->second;
}
} // namespace Commands