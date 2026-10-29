/**
 * @file
 * @brief Application-wide logging targets.
 */

#pragma once

#include <string>

/**
 * @brief Route the default spdlog logger to the console and, optionally, a file.
 *
 * The console shows time, level and message in colour on stderr. A file, when given, receives
 * every message with the date and is appended to across runs.
 *
 * @param[in] level Lowest level shown: trace, debug, info, warn, error, critical or off.
 * @param[in] file Log file path; empty for console only.
 * @throws std::runtime_error For an unknown level or a file that cannot be opened.
 */
void setupLogging(const std::string& level, const std::string& file = "");