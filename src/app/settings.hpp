/**
 * @file
 * @brief CLI settings and nonmutating Home Assistant connection-setting lookup.
 */

#pragma once

#include "config/configuration.hpp"
#include <algorithm>
#include <string>
#include <thread>

struct Settings {
    std::string model = "models/ggml-base.en.bin";
    int threads = static_cast<int>(std::max(1u, std::min(4u, std::thread::hardware_concurrency())));
    float threshold = 0.01f;
    int silenceMs = 700;
    bool gpu = true;
    std::string phrases = Config::defaultFile();
    bool listDevices = false;
    bool checkPhrases = false;
    std::string parseText;
    bool parseOnly = false;
};

/**
 * @brief Parse CLI arguments into settings without loading models or contacting devices.
 *
 * Accepts one positional model path, optional recognition settings, an alternate
 * phrase file, and at most one discovery/check/parse-only mode. Numeric options
 * must consume their entire argument and satisfy the documented ranges.
 *
 * @param[in] argc Number of entries in argv, including the executable name.
 * @param[in] argv Process argument array; contents are not modified.
 * @return Parsed settings combined with defaults for unspecified options.
 * @throws std::runtime_error For unknown/missing options, duplicate modes/model paths,
 *                            trailing numeric characters, or out-of-range settings.
 * @throws std::invalid_argument If a numeric argument cannot be converted.
 * @throws std::out_of_range If a numeric conversion exceeds its target type.
 * @note Performs no file reads, environment writes, model initialization, or HTTP.
 */
Settings parseArguments(int argc, char* argv[]);

struct HomeAssistantSettings {
    std::string url = "http://wyvern:8123";
    std::string token;
};

/**
 * @brief Read connection settings from an optional dotenv-style file and the environment.
 *
 * Recognizes HOME_ASSISTANT_URL and HOME_ASSISTANT_TOKEN, trims surrounding
 * whitespace, accepts an optional "export " prefix, and removes matching outer
 * quotes. Process environment values override file values, including empty values.
 * A missing/unreadable file supplies no file overrides; unknown keys are ignored.
 *
 * @param[in] path Optional settings-file path, defaulting to ".env".
 * @return Connection settings; URL defaults to the local installation's configured
 *         host and token remains empty when no value is supplied.
 * @throws std::runtime_error For a non-comment file entry without an equals sign.
 * @note Reads filesystem/environment state but never calls setenv, modifies the
 *       file, contacts Home Assistant, or prints credentials.
 */
HomeAssistantSettings readHomeAssistantSettings(const std::string& path = ".env");
/**
 * @brief Write the application's supported invocation syntax to standard output.
 *
 * @param[in] executable Executable name or path displayed at the start of the usage line.
 * @note Prints help only; does not parse arguments, open devices, or exit the process.
 */
void printUsage(const std::string& executable);