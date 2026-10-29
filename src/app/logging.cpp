/**
 * @file
 * @brief Console and file sinks for the default spdlog logger.
 */

#include "logging.hpp"

#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>
#include <stdexcept>
#include <vector>

void setupLogging(const std::string& level, const std::string& file) {
    const auto threshold = spdlog::level::from_str(level);

    // from_str maps unknown names to off, so only "off" itself may yield it.
    if(threshold == spdlog::level::off && level != "off") {
        throw std::runtime_error("Unknown log level: " + level);
    }

    auto console = std::make_shared<spdlog::sinks::stderr_color_sink_mt>();
    console->set_pattern("[%H:%M:%S] [%^%l%$] %v");
    std::vector<spdlog::sink_ptr> sinks{console};

    if(!file.empty()) {
        auto disk = std::make_shared<spdlog::sinks::basic_file_sink_mt>(file);
        disk->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] %v");
        sinks.push_back(disk);
    }

    auto logger = std::make_shared<spdlog::logger>("smarthome", sinks.begin(), sinks.end());
    logger->set_level(threshold);
    logger->flush_on(spdlog::level::warn);
    spdlog::set_default_logger(logger);
}