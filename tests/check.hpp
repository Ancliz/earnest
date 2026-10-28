/**
 * @file
 * @brief Minimal assertion helpers shared by the test executables.
 */

#pragma once

#include <stdexcept>
#include <string>

/// Assertions evaluated so far.
inline int checks = 0;

/**
 * @brief Count an assertion and throw its description when it fails.
 * @throws std::runtime_error If passed is false.
 */
inline void check(bool passed, const std::string& description) {
    ++checks;

    if(!passed) {
        throw std::runtime_error(description);
    }
}

/**
 * @brief Check that an action throws a std::exception.
 * @throws std::runtime_error If the action completes without throwing.
 */
template <typename Action> void checkThrows(Action action, const std::string& description) {
    bool threw = false;

    try {
        action();
    } catch(const std::exception&) {
        threw = true;
    }

    check(threw, description);
}