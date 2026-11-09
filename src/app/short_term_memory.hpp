/**
 * @file
 * @brief What the assistant remembers between requests, for follow-ups like "turn it up".
 */

#pragma once

#include "exchange.hpp"

#include <chrono>
#include <deque>
#include <string>
#include <vector>

/**
 * @brief The last few commands and the device most recently commanded, for a few minutes after.
 */
class ShortTermMemory {

    /// Monotonic clock that requests are timed on.
    using Clock = std::chrono::steady_clock;

    /// How long a request is remembered; long enough for a follow-up, short enough not to hit the wrong thing later.
    static constexpr auto lifetime = std::chrono::minutes(5);

    /// Requests kept for a parser to read; each one lengthens every prompt.
    static constexpr std::size_t maxRequests = 4;

    /**
     * @brief A remembered request and when it was heard.
     */
    struct Entry {

        /// The request and its outcome.
        Commands::PastRequest past;

        /// When it was heard.
        Clock::time_point heardAt;
    };

    /// Remembered requests, oldest first.
    std::deque<Entry> entries;

    /// Entity most recently commanded successfully; "" when there is none.
    std::string device;

    /// When the request for that device was heard.
    Clock::time_point deviceHeardAt;

public:
    /**
     * @brief Take in a finished exchange. Only a command that worked and targets a device becomes "it".
     */
    void remember(const Exchange& exchange) {
        if(!exchange.result) {
            return;
        }

        entries.push_back({{exchange.request, exchange.result->command, exchange.result->success}, exchange.heardAt});

        if(entries.size() > maxRequests) {
            entries.pop_front();
        }

        if(exchange.result->success && !exchange.result->command.entity.empty()) {
            device = exchange.result->command.entity;
            deviceHeardAt = exchange.heardAt;
        }
    }

    /**
     * @brief The entity a request heard at the given time would mean by "it", or "".
     */
    std::string recall(Clock::time_point now) const { return now - deviceHeardAt < lifetime ? device : ""; }

    /**
     * @brief The requests still remembered at the given time, oldest first.
     */
    std::vector<Commands::PastRequest> recent(Clock::time_point now) const {
        std::vector<Commands::PastRequest> history;

        for(const auto& entry : entries) {
            if(now - entry.heardAt < lifetime) {
                history.push_back(entry.past);
            }
        }

        return history;
    }
};