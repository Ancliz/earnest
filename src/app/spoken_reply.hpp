/**
 * @file
 * @brief What to say aloud once a command has run.
 */

#pragma once

#include "commands/command.hpp"

#include <cmath>
#include <regex>
#include <string>

/**
 * @brief The words for a request that produced no command.
 * @param[in] reason The parser's explanation; one already addressing "sir" is said as written.
 */
inline std::string spokenFailure(const std::string& reason) {
    static const std::regex addressed("\\bsir\\b", std::regex::icase);
    return std::regex_search(reason, addressed) ? reason : reason + ", sir.";
}

/**
 * @brief The words for a finished command.
 *
 * In order: the executor's answer, a failure notice, the volume actually set (read from the
 * resolved command, so relative changes report their result), the parser's wording, a default.
 *
 * @param[in] asked Command the parser proposed.
 * @param[in] result Outcome of executing it.
 */
inline std::string spokenReply(const Commands::Command& asked, const Commands::Result& result) {
    if(!result.reply.empty()) {
        return result.reply;
    } else if(!result.success) {
        return "Command failed, sir.";
    }

    const auto& done = result.command;

    if(done.domain == "media_player" && done.service == "volume_set" && done.data.value("volume_level", Commands::json()).is_number()) {
        return "Volume set to " + std::to_string(std::lround(done.data["volume_level"].get<double>() * 100)) + " percent.";
    }

    return asked.reply.empty() ? "Command sent, sir" : asked.reply;
}