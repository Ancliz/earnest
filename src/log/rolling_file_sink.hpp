/**
 * @file
 * @brief A log file that starts afresh each day or hour, when it grows too big, or whichever comes first.
 */

#pragma once

#include <ctime>
#include <filesystem>
#include <mutex>
#include <spdlog/details/file_helper.h>
#include <spdlog/sinks/base_sink.h>
#include <string>

namespace Log {

/**
 * @brief How often a log file starts afresh regardless of its size.
 */
enum class Period { None, Daily, Hourly };

/**
 * @brief When a log file rolls over, and how many old ones to keep.
 */
struct RollingPolicy {

    /// Start a new file each day or hour, named for it.
    Period every = Period::None;

    /// Bytes a file may reach before it rolls over within its period; 0 sets no limit.
    std::size_t maxBytes = 0;

    /// Hour of the day a daily file rolls over.
    int hour = 0;

    /// Minute past that hour.
    int minute = 0;

    /// Old files kept, oldest dropped first; 0 keeps them all.
    std::size_t keep = 3;
};

/**
 * @brief Appends to a file, rolling over on time or size, whichever comes first.
 *
 * The file is name.ext, or name_YYYY-MM-DD.ext daily and name_YYYY-MM-DD_HH.ext hourly. Rolling
 * over on size moves it to name.1.ext (or name_YYYY-MM-DD.1.ext), older ones moving up a number.
 */
class RollingFileSink final : public spdlog::sinks::base_sink<std::mutex> {

    /// The configured path, whose folder, name and extension every file shares.
    std::filesystem::path base;

    /// When to roll over.
    RollingPolicy policy;

    /// The open file.
    spdlog::details::file_helper file;

    /// The current period's date, or date and hour, for file names; empty without a period.
    std::string stamp;

    /// When the current period ends.
    std::time_t periodEnd = 0;

    /// Bytes in the open file, counted as written, as buffered writes don't show in its size yet.
    std::size_t bytes = 0;

    /**
     * @brief The current file, or its size roll-over number n.
     */
    std::filesystem::path fileFor(std::size_t number = 0) const;

    /**
     * @brief Open the file for the period containing a time.
     */
    void startPeriod(std::time_t now);

    /**
     * @brief Move the full file aside and start an empty one in the same period.
     */
    void rollBySize();

    /**
     * @brief Delete the oldest log files beyond the number kept.
     */
    void removeOld();

protected:
    /**
     * @brief Roll over if needed, then write one formatted message.
     */
    void sink_it_(const spdlog::details::log_msg& message) override;

    /**
     * @brief Flush the open file.
     */
    void flush_() override;

public:
    /**
     * @brief Open the file for now.
     * @throws spdlog::spdlog_ex If it cannot be opened.
     */
    RollingFileSink(const std::string& path, RollingPolicy policy);
};
} // namespace Log