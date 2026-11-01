/**
 * @file
 * @brief Access to recorded Home Assistant responses and the test installation.
 */

#pragma once

#include "config/configuration.hpp"

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

/**
 * @brief The shipped phrase file overlaid with the test installation's aliases and AC policy.
 */
inline nlohmann::json readTestConfiguration() {
    const auto home = Config::readYamlFile(std::string(SMARTHOME_TEST_FIXTURES) + "/home.yaml");
    return Config::mergeConfiguration(Config::readYamlFile(Config::defaultPhraseFile), home);
}