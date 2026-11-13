/**
 * @file
 * @brief Regression suite for log destinations, line format, colour and rolling files.
 */

#include "check.hpp"
#include "log/log.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <unistd.h>

namespace fs = std::filesystem;
using std::string;

namespace {
/**
 * @brief The whole of a file.
 */
string contents(const fs::path& path) {
    std::ifstream file(path);
    std::ostringstream text;
    text << file.rdbuf();
    return text.str();
}
} // namespace

/**
 * @brief Verify that each destination gets every line, formatted for it, and that files roll over.
 */
int main() {
    const auto folder = fs::temp_directory_path() / ("earnest-log-" + std::to_string(getpid()));
    fs::create_directories(folder);
    const auto plain = folder / "plain.log";
    const auto coloured = folder / "coloured.log";
    const auto rolled = folder / "rolled.log";
    const auto warnings = folder / "warnings.log";

    checkThrows([] { Log::setup("loud", {}); }, "an unknown level is rejected");
    auto colourful = Log::Destination::file(coloured.string());
    colourful.colour = true;
    auto bySize = Log::Destination::file(rolled.string());
    bySize.rolling.maxBytes = 2048;
    bySize.rolling.keep = 2;
    auto daily = Log::Destination::file((folder / "daily.log").string());
    daily.rolling.every = Log::Period::Daily;
    auto hourly = Log::Destination::file((folder / "hourly.log").string());
    hourly.rolling.every = Log::Period::Hourly;
    Log::setup("debug", {Log::Destination::file(plain.string()), colourful, bySize, Log::Destination::file(warnings.string(), "warn"), daily, hourly});
    Log::hear()->info("turn off the q800");
    Log::think()->warn("careful");

    for(int line = 0; line < 200; ++line) {
        Log::app()->debug("line {} with enough words to fill a rolled file quickly", line);
    }

    spdlog::apply_all([](const std::shared_ptr<spdlog::logger>& logger) { logger->flush(); });
    const auto text = contents(plain);
    check(text.find("] [info] [hear]  turn off the q800") != string::npos && text.find("] [warning] [think] careful") != string::npos, "lines read [level] [category] message, padded to the longest category");
    check(text.find('\033') == string::npos && contents(coloured).find("\033[") != string::npos, "files are plain unless colour is asked for");
    check(fs::exists(rolled) && fs::exists(folder / "rolled.1.log") && fs::exists(folder / "rolled.2.log") && !fs::exists(folder / "rolled.3.log"), "a file rolls over, keeping only as many old files as asked");
    check(fs::file_size(rolled) <= 2048, "a rolling file stays under its size");
    const auto important = contents(warnings);
    check(important.find("careful") != string::npos && important.find("q800") == string::npos && important.find("line 1") == string::npos, "each destination has its own level");
    bool dated = false;
    bool hourStamped = false;

    for(const auto& entry : fs::directory_iterator(folder)) {
        const auto name = entry.path().filename().string();
        dated = dated || (name.rfind("daily_", 0) == 0 && name.size() == string("daily_2026-11-13.log").size());
        hourStamped = hourStamped || (name.rfind("hourly_", 0) == 0 && name.size() == string("hourly_2026-11-13_19.log").size());
    }

    check(dated && hourStamped, "daily and hourly files are named for their day and hour");
    spdlog::drop_all();

    // Feed the sink messages stamped at chosen times, rather than waiting for days to pass.
    const auto both = folder / "both";
    fs::create_directories(both);
    Log::RollingPolicy policy;
    policy.every = Log::Period::Daily;
    policy.hour = 6;
    policy.maxBytes = 300;
    policy.keep = 10;
    Log::RollingFileSink sink((both / "earnest.log").string(), policy);
    sink.set_pattern("%v");
    std::tm day{};
    day.tm_year = 2026 - 1900;
    day.tm_mon = 10;
    day.tm_mday = 13;
    day.tm_isdst = -1;
    const auto at = [&](int dayOfMonth, int hour) {
        std::tm when = day;
        when.tm_mday = dayOfMonth;
        when.tm_hour = hour;
        return std::chrono::system_clock::from_time_t(std::mktime(&when));
    };
    const auto write = [&](std::chrono::system_clock::time_point when, int lines) {
        for(int line = 0; line < lines; ++line) {
            sink.log(spdlog::details::log_msg(when, {}, "app", spdlog::level::info, "a line long enough to fill the file up quickly"));
        }
    };
    write(at(13, 10), 20);
    write(at(14, 5), 1);
    write(at(14, 7), 1);
    sink.flush();
    const auto named = [&](const char* name) { return fs::exists(both / name); };
    check(named("earnest_2026-11-13.log") && named("earnest_2026-11-13.1.log") && named("earnest_2026-11-13.2.log"), "a file rolls over on size within its day");
    check(named("earnest_2026-11-14.log") && !named("earnest_2026-11-14.1.log"), "and on time, starting the next day's file at the roll-over hour");
    check(contents(both / "earnest_2026-11-13.log").find("a line") != string::npos, "before the roll-over hour still belongs to the previous day");

    Log::RollingPolicy few = policy;
    few.keep = 2;
    Log::RollingFileSink pruning((both / "earnest.log").string(), few);
    pruning.set_pattern("%v");
    pruning.log(spdlog::details::log_msg(at(16, 10), {}, "app", spdlog::level::info, "later"));
    size_t files = 0;

    for(const auto& entry : fs::directory_iterator(both)) {
        files += entry.path().extension() == ".log";
    }

    check(files == 3, "only as many old files as asked are kept, besides the current one");
    fs::remove_all(folder);
    std::cout << checks << " log checks passed\n";
}