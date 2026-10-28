/**
 * @file
 * @brief Services a command may name, shared by parsers and executors.
 */

#pragma once

#include <map>
#include <set>
#include <string>

namespace Commands {
/**
 * @brief Data fields one service accepts.
 */
struct ServiceFields {
    
    /// Every field the service accepts besides entity_id.
    std::set<std::string> allowed;

    /// Fields that must be present.
    std::set<std::string> required;
};

/**
 * @brief Supported services by domain, then by service name.
 */
using ServiceCatalog = std::map<std::string, std::map<std::string, ServiceFields>>;

/**
 * @brief Every service a command may name.
 *
 * Parsers use it to validate grammar or describe tools to a model; executors reject anything
 * else. volume_up and volume_down accept an internal amount (0..1) that executors resolve
 * into volume_set.
 */
inline const ServiceCatalog& serviceCatalog() {
    static const ServiceCatalog catalog = {
        {"media_player",
         {{"volume_set", {{"volume_level"}, {"volume_level"}}},
          {"volume_up", {{"amount"}, {}}},
          {"volume_down", {{"amount"}, {}}},
          {"volume_mute", {{"is_volume_muted"}, {"is_volume_muted"}}},
          {"media_play", {}},
          {"media_pause", {}},
          {"media_stop", {}},
          {"media_play_pause", {}},
          {"media_next_track", {}},
          {"media_previous_track", {}},
          {"turn_on", {}},
          {"turn_off", {}}}},
        {"climate",
         {{"turn_on", {}},
          {"turn_off", {}},
          {"set_temperature", {{"temperature"}, {"temperature"}}},
          {"set_hvac_mode", {{"hvac_mode"}, {"hvac_mode"}}},
          {"set_fan_mode", {{"fan_mode"}, {"fan_mode"}}}}},
        {"light", {{"turn_on", {{"brightness_pct"}, {}}}, {"turn_off", {}}}},
        {"switch", {{"turn_on", {}}, {"turn_off", {}}}},
        {"fan", {{"turn_on", {}}, {"turn_off", {}}, {"set_percentage", {{"percentage"}, {"percentage"}}}}},
        {"cover",
         {{"open_cover", {}}, {"close_cover", {}}, {"set_cover_position", {{"position"}, {"position"}}}}},
        {"select", {{"select_option", {{"option"}, {"option"}}}}}};
    return catalog;
}

/**
 * @brief Look up the fields of domain.service.
 * @return Catalog entry, or nullptr when commands may not name that service.
 */
inline const ServiceFields* findService(const std::string& domain, const std::string& service) {
    const auto group = serviceCatalog().find(domain);

    if(group == serviceCatalog().end()) {
        return nullptr;
    }

    const auto entry = group->second.find(service);
    return entry == group->second.end() ? nullptr : &entry->second;
}
} // namespace Commands