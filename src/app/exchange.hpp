/**
 * @file
 * @brief The record of one spoken request, from hearing it to answering it.
 */

#pragma once

#include "commands/command.hpp"

#include <chrono>
#include <functional>
#include <optional>
#include <string>

/**
 * @brief What happened to one request addressed to the assistant.
 */
struct Exchange {

    /// When the utterance finished.
    std::chrono::steady_clock::time_point heardAt;

    /// Words as transcribed, including any wake word.
    std::string transcript;

    /// Words given to the parser.
    std::string request;

    /// What the parser made of them.
    Commands::ParseResult parsed;

    /// Outcome of executing the command; empty when there was none.
    std::optional<Commands::Result> result;

    /// Words said in response; empty when nothing was said.
    std::string spoken;
};

/**
 * @brief Told about each finished exchange, on the worker thread.
 */
using ExchangeObserver = std::function<void(const Exchange&)>;