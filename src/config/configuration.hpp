/**
 * @file
 * @brief Bounded YAML decoding and a file-backed configuration with hot reload.
 */

#pragma once

#include <functional>
#include <string>
#include <nlohmann/json.hpp>

#ifndef SMARTHOME_PHRASE_FILE
/// Phrase file path; the build supplies the source tree's config/phrases.yaml.
#define SMARTHOME_PHRASE_FILE "config/phrases.yaml"
#endif

#ifndef SMARTHOME_HOME_FILE
/// Installation file path; the build supplies the source tree's config/home.yaml.
#define SMARTHOME_HOME_FILE "config/home.yaml"
#endif

namespace Config {
    
/// JSON value type produced by YAML decoding.
using json = nlohmann::json;

/**
 * @brief Phrase file used when --phrases is not given.
 */
constexpr const char* defaultPhraseFile = SMARTHOME_PHRASE_FILE;

/**
 * @brief Installation file (aliases, voice scope, AC policy) used when --home is not given.
 */
constexpr const char* defaultHomeFile = SMARTHOME_HOME_FILE;

/**
 * @brief Decode one YAML document into JSON.
 *
 * Accepts at most 256 KiB, 16 nesting levels and 32768 nodes after alias expansion, and rejects
 * duplicate or non-scalar keys and extra documents. Plain true/false/null and finite numbers
 * become typed values; quoted scalars stay strings.
 *
 * @throws std::runtime_error For invalid YAML or exceeded limits.
 */
json readYaml(const std::string& text);

/**
 * @brief Read a file and decode it with readYaml().
 * @throws std::runtime_error If the file cannot be read or decoded.
 */
json readYamlFile(const std::string& path);

/**
 * @brief Overlay an installation file on the phrase file: objects merge, lists append, and other
 *        values replace.
 */
json mergeConfiguration(json base, const json& overlay);

/**
 * @brief A phrase file, overlaid by an optional installation file, pushed to its consumers on load
 *        and whenever either file's text changes.
 */
class Configuration {
    /// Phrase file read on construction and on each reload.
    std::string path;
    /// Installation file merged over it; empty or missing for none.
    std::string homePath;
    /// Texts of the last load attempt, whether applied or rejected.
    std::string lastText;
    /// Configuration currently applied to every consumer.
    json active;
    /// Configures every consumer; throws to reject a candidate.
    std::function<void(const json&)> apply;

    /**
     * @brief Decode and apply text, returning consumers to the active configuration on rejection.
     * @throws std::exception If the text cannot be decoded or a consumer rejects it.
     */
    void load(const std::string& text);

    /**
     * @brief Both files' texts, joined so a change to either is seen; a missing home file is empty.
     */
    std::string readTexts() const;

public:
    /**
     * @brief Load a file and pass it to the consumers.
     * @param[in] path Phrase file to read now and on each reload.
     * @param[in] apply Configures every consumer and throws to reject a configuration.
     * @param[in] homePath Installation file merged over the phrase file, if it exists.
     * @throws std::exception If a file cannot be read, decoded, or applied.
     */
    Configuration(std::string path, std::function<void(const json&)> apply, std::string homePath = "");

    /**
     * @brief Apply the file again if its text changed since the last attempt.
     *
     * Rejected text is not retried until the file changes again.
     *
     * @return True when changed text was applied; false when the text is unchanged.
     * @throws std::exception If the file cannot be read or the changed text is rejected. The
     *                        previous configuration then stays active in every consumer.
     */
    bool reloadIfChanged();

    /**
     * @brief The configuration most recently accepted by every consumer.
     */
    const json& data() const {
        return active;
    }
};
} // namespace Config