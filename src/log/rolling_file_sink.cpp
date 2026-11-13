/**
 * @file
 * @brief A log file that starts afresh each day or hour, when it grows too big, or whichever comes first.
 */

#include "rolling_file_sink.hpp"

#include <algorithm>
#include <limits>
#include <regex>
#include <vector>

namespace fs = std::filesystem;
using std::size_t;
using std::string;

namespace Log {
namespace {
/**
 * @brief Text with regex special characters escaped, to match it literally.
 */
string literally(const string& text) {
    static const std::regex special(R"([.^$|()\[\]{}*+?\\])");
    return std::regex_replace(text, special, R"(\$&)");
}
} // namespace

RollingFileSink::RollingFileSink(const string& path, RollingPolicy policy) : base(path), policy(policy) {
    startPeriod(std::time(nullptr));
}

fs::path RollingFileSink::fileFor(size_t number) const {
    const auto name = base.stem().string() + (stamp.empty() ? "" : "_" + stamp) + (number ? "." + std::to_string(number) : "");
    return base.parent_path() / (name + base.extension().string());
}

void RollingFileSink::startPeriod(std::time_t now) {
    std::tm local{};
    localtime_r(&now, &local);
    local.tm_sec = 0;
    local.tm_isdst = -1;

    if(policy.every == Period::Hourly) {
        local.tm_min = 0;
        const auto start = std::mktime(&local);
        periodEnd = start + 60 * 60;
        char text[32];
        std::strftime(text, sizeof text, "%Y-%m-%d_%H", &local);
        stamp = text;
    } else if(policy.every == Period::Daily) {
        local.tm_hour = policy.hour;
        local.tm_min = policy.minute;

        // Before today's roll-over time, the period began at yesterday's.
        if(std::mktime(&local) > now) {
            local.tm_mday -= 1;
        }

        std::tm start = local;
        std::mktime(&start);
        std::tm end = start;
        end.tm_mday += 1;
        periodEnd = std::mktime(&end);
        char text[32];
        std::strftime(text, sizeof text, "%Y-%m-%d", &start);
        stamp = text;
    } else {
        periodEnd = std::numeric_limits<std::time_t>::max();
        stamp.clear();
    }

    file.close();
    file.open(fileFor().string(), false);
    bytes = file.size();
}

void RollingFileSink::rollBySize() {
    std::error_code ignored;
    file.close();
    size_t highest = 0;

    while(fs::exists(fileFor(highest + 1))) {
        ++highest;
    }

    // Move each older file up one number, then the full file to 1.
    for(auto number = highest; number >= 1; --number) {
        fs::rename(fileFor(number), fileFor(number + 1), ignored);
    }

    fs::rename(fileFor(), fileFor(1), ignored);
    file.open(fileFor().string(), false);
    bytes = 0;
}

void RollingFileSink::removeOld() {
    if(policy.keep == 0) {
        return;
    }

    // Only this log's own files: name, then any date and hour, then any roll-over number, then the extension.
    const std::regex ours("^" + literally(base.stem().string()) + R"((_\d{4}-\d{2}-\d{2}(_\d{2})?)?(\.\d+)?)" + literally(base.extension().string()) + "$");
    std::vector<fs::directory_entry> old;
    std::error_code ignored;
    const auto folder = base.parent_path().empty() ? fs::path(".") : base.parent_path();

    for(const auto& entry : fs::directory_iterator(folder, ignored)) {
        if(entry.path().filename() != fileFor().filename() && std::regex_match(entry.path().filename().string(), ours)) {
            old.push_back(entry);
        }
    }

    std::sort(old.begin(), old.end(), [](const fs::directory_entry& a, const fs::directory_entry& b) { return a.last_write_time() > b.last_write_time(); });

    for(size_t i = policy.keep; i < old.size(); ++i) {
        fs::remove(old[i].path(), ignored);
    }
}

void RollingFileSink::sink_it_(const spdlog::details::log_msg& message) {
    const auto now = std::chrono::system_clock::to_time_t(message.time);

    if(now >= periodEnd) {
        startPeriod(now);
        removeOld();
    }

    spdlog::memory_buf_t formatted;
    formatter_->format(message, formatted);

    if(policy.maxBytes > 0 && bytes > 0 && bytes + formatted.size() > policy.maxBytes) {
        rollBySize();
        removeOld();
    }

    file.write(formatted);
    bytes += formatted.size();
}

void RollingFileSink::flush_() {
    file.flush();
}
} // namespace Log