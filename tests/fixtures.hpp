/**
 * @file
 * @brief Access to recorded Home Assistant responses used as test fixtures.
 */

#pragma once

#include <fstream>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>

#ifndef SMARTHOME_TEST_FIXTURES
#define SMARTHOME_TEST_FIXTURES "tests/fixtures"
#endif

/**
 * @brief Parse a JSON fixture from tests/fixtures.
 * @throws std::runtime_error If the file is missing or not JSON.
 */
inline nlohmann::json readFixture(const std::string& name) {
    std::ifstream file(std::string(SMARTHOME_TEST_FIXTURES) + "/" + name);
    const auto fixture = nlohmann::json::parse(file, nullptr, false);

    if(fixture.is_discarded()) {
        throw std::runtime_error("Cannot read fixture " + name);
    }

    return fixture;
}