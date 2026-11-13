/**
 * @file
 * @brief One logger per kind of message, so the console can tell them apart at a glance.
 *
 * Libraries only log through the accessors; the application calls Log::setup() once to decide
 * where the messages go.
 */

#pragma once

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include "rolling_file_sink.hpp"

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
 * @brief One place log lines are written to, with its own level and, for a file, when it rolls over.
 */
struct Destination {

    /**
     * @brief The terminal (standard error) or a file.
     */
    enum class Kind { Terminal, File };

    /// Where the lines go.
    Kind kind = Kind::Terminal;

    /// File path; unused for the terminal.
    std::string path;

    /// Lowest level written here, such as "debug"; empty uses the level given to setup().
    std::string level;

    /// When a file rolls over: each day or hour, on reaching a size, or whichever comes first.
    RollingPolicy rolling;

    /// Whether to colour the level and category. Unset, a terminal decides for itself and a file never colours.
    std::optional<bool> colour;

    /**
     * @brief The terminal, coloured if it can show colour.
     */
    static Destination terminal(std::string level = "") {
        Destination destination;
        destination.level = std::move(level);
        return destination;
    }

    /**
     * @brief A file, appended to across runs, which never rolls over unless its rolling policy is set.
     */
    static Destination file(std::string path, std::string level = "") {
        Destination destination;
        destination.kind = Kind::File;
        destination.path = std::move(path);
        destination.level = std::move(level);
        return destination;
    }
};

/**
 * @brief Register the category loggers, writing each line to every destination whose level allows it.
 *
 * Each line shows the time, the level, a category (hear, think, act, say or app) and the
 * message, with the category padded so messages line up. Files add the date. A terminal shows
 * colour only when standard error is one, NO_COLOR is unset and TERM isn't "dumb".
 *
 * @param[in] level Level for destinations that don't set their own: trace, debug, info, warn, error, critical or off.
 * @param[in] destinations Where to write; none discards everything.
 * @throws std::runtime_error For an unknown level, or a spdlog_ex for a file that cannot be opened.
 */
void setup(const std::string& level, const std::vector<Destination>& destinations);
} // namespace Log