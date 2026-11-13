/**
 * @file
 * @brief One logger per kind of message, so the console can tell them apart at a glance.
 *
 * Libraries only log through the accessors; the application calls Log::setup() once to decide
 * where the messages go.
 */

#pragma once

#include <memory>
#include <string>
#include <spdlog/spdlog.h>

/**
 * @brief Loggers by category: what was heard, what was decided, what was done, what was said, and the rest.
 *
 * Each falls back to the default logger until setup() registers it, as in the tests.
 */
namespace Log {
/**
 * @brief The logger registered under a category name, or the default one.
 */
inline std::shared_ptr<spdlog::logger> category(const char* name) {
    auto logger = spdlog::get(name);
    return logger ? logger : spdlog::default_logger();
}

/**
 * @brief Speech heard, and what was made of it before parsing.
 */
inline std::shared_ptr<spdlog::logger> hear() { return category("hear"); }

/**
 * @brief Parsing and the model.
 */
inline std::shared_ptr<spdlog::logger> think() { return category("think"); }

/**
 * @brief Commands sent and the services they go to.
 */
inline std::shared_ptr<spdlog::logger> act() { return category("act"); }

/**
 * @brief What is said aloud.
 */
inline std::shared_ptr<spdlog::logger> say() { return category("say"); }

/**
 * @brief Startup, configuration and everything else.
 */
inline std::shared_ptr<spdlog::logger> app() { return category("app"); }

/**
 * @brief Register the category loggers, writing to the console and, optionally, a file.
 *
 * Each line shows the time, the level, a category (hear, think, act, say or app) and the
 * message, with the category padded so messages line up. On a terminal the level and category
 * are coloured. A file, when given, receives every message with the date, without colour, and is
 * appended to across runs.
 *
 * @param[in] level Lowest level shown: trace, debug, info, warn, error, critical or off.
 * @param[in] file Log file path; empty for console only.
 * @throws std::runtime_error For an unknown level or a file that cannot be opened.
 */
void setup(const std::string& level, const std::string& file = "");
} // namespace Log