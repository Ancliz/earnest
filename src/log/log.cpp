/**
 * @file
 * @brief Console and file sinks shared by one logger per category.
 */

#include "log.hpp"

#include <cstdio>
#include <spdlog/pattern_formatter.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_sinks.h>
#include <spdlog/spdlog.h>
#include <stdexcept>
#include <string_view>
#include <unistd.h>
#include <vector>

using std::make_shared;
using std::string;
using std::string_view;

namespace {
/// Categories with their console colours; the first is the default logger's.
constexpr std::pair<const char*, const char*> categories[] = {
    {"app", "\033[94m"}, {"hear", "\033[96m"}, {"think", "\033[95m"}, {"act", "\033[93m"}, {"say", "\033[92m"}};

/// Characters in the longest category name, "think".
constexpr std::size_t longestCategory = 5;

/**
 * @brief Writes "[level] [category]", padded so messages line up, coloured on a terminal.
 */
class CategoryFlag final : public spdlog::custom_flag_formatter {

    /// Whether to colour the level and category.
    bool colour;

    /**
     * @brief Append "[text]" with only the text coloured, if wanted.
     */
    void bracketed(spdlog::memory_buf_t& dest, string_view text, const char* tint) const {
        const string word = colour && *tint ? tint + string(text) + "\033[0m" : string(text);
        const string whole = "[" + word + "]";
        dest.append(whole.data(), whole.data() + whole.size());
    }

public:
    /**
     * @brief Format with or without colour.
     */
    explicit CategoryFlag(bool colour) : colour(colour) {}

    /**
     * @brief Write the level and category for one message.
     */
    void format(const spdlog::details::log_msg& message, const std::tm&, spdlog::memory_buf_t& dest) override {
        const string_view name(message.logger_name.data(), message.logger_name.size());
        const auto level = spdlog::level::to_string_view(message.level);
        // spdlog's usual level colours; categories use the bright shades so the two never match.
        static const char* const levelTints[] = {"\033[37m", "\033[36m", "\033[32m", "\033[1;33m", "\033[1;31m", "\033[1;41m", ""};
        const char* levelTint = levelTints[message.level];
        const char* tint = "";

        for(const auto& [category, code] : categories) {
            tint = name == category ? code : tint;
        }

        // One space between level and category, then the category padded to the longest, so its messages line up.
        bracketed(dest, string_view(level.data(), level.size()), levelTint);
        dest.push_back(' ');
        bracketed(dest, name, tint);
        const string padding(name.size() < longestCategory ? longestCategory - name.size() : 0, ' ');
        dest.append(padding.data(), padding.data() + padding.size());
    }

    /**
     * @brief A copy for another formatter.
     */
    std::unique_ptr<spdlog::custom_flag_formatter> clone() const override { return spdlog::details::make_unique<CategoryFlag>(colour); }
};

/**
 * @brief A formatter for "[time] category [level] message".
 */
std::unique_ptr<spdlog::formatter> lineFormat(const char* time, bool colour) {
    auto formatter = std::make_unique<spdlog::pattern_formatter>();
    formatter->add_flag<CategoryFlag>('*', colour).set_pattern(string(time) + " %* %v");
    return formatter;
}
} // namespace

namespace Log {
void setup(const std::string& level, const std::string& file) {
    const auto threshold = spdlog::level::from_str(level);

    // from_str maps unknown names to off, so only "off" itself may yield it.
    if(threshold == spdlog::level::off && level != "off") {
        throw std::runtime_error("Unknown log level: " + level);
    }

    auto console = make_shared<spdlog::sinks::stderr_sink_mt>();
    console->set_formatter(lineFormat("[%H:%M:%S]", isatty(fileno(stderr))));
    std::vector<spdlog::sink_ptr> sinks{console};

    if(!file.empty()) {
        auto disk = make_shared<spdlog::sinks::basic_file_sink_mt>(file);
        disk->set_formatter(lineFormat("[%Y-%m-%d %H:%M:%S.%e]", false));
        sinks.push_back(disk);
    }

    for(const auto& [name, code] : categories) {
        auto logger = make_shared<spdlog::logger>(name, sinks.begin(), sinks.end());
        logger->set_level(threshold);
        logger->flush_on(spdlog::level::warn);
        spdlog::drop(name);
        spdlog::register_logger(logger);

        if(string_view(name) == "app") {
            spdlog::set_default_logger(logger);
        }
    }
}
} // namespace Log