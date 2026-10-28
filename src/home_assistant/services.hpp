/**
 * @file
 * @brief Immutable domain/service catalog shared by parsing and execution.
 */

#pragma once

#include <map>
#include <set>
#include <string>

namespace HomeServices {
struct Service {
    std::set<std::string> fields;
    std::set<std::string> required;
};

/**
 * @brief Look up the application's allowed fields for one domain/service pair.
 *
 * Uses an immutable process-local catalog shared by grammar compilation and
 * executor validation. This defines application-supported requests, independently
 * of which services a live Home Assistant instance currently advertises.
 *
 * @param[in] domain Home Assistant entity domain.
 * @param[in] service Service name without a URL prefix or domain.
 * @return Pointer to the catalog entry, valid for program lifetime, or nullptr for
 *         unsupported combinations. The returned entry must not be modified.
 * @note Performs no HTTP or application-state mutation after catalog initialization.
 */
inline const Service* find(const std::string& domain, const std::string& service) {
    static const std::map<std::string, std::map<std::string, Service>> catalog = {
        {"media_player",
         {{"volume_set", {{"volume_level"}, {"volume_level"}}},
          {"volume_up", {}},
          {"volume_down", {}},
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
    const auto group = catalog.find(domain);

    if(group == catalog.end()) {
        return nullptr;
    }

    const auto entry = group->second.find(service);
    return entry == group->second.end() ? nullptr : &entry->second;
}
} // namespace HomeServices
