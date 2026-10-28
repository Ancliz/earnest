/**
 * @file
 * @brief Bounded YAML decoding and explicit transactional configuration loading.
 */

#pragma once

#include <functional>
#include <string>
#include <nlohmann/json.hpp>

namespace Config {
using json = nlohmann::json;

/**
 * @brief Decode one bounded YAML document into JSON without reading a file.
 *
 * Accepts 1..262144 bytes, at most 16 nesting levels, and at most 32768 expanded
 * nodes. Duplicate mapping keys, non-scalar keys, additional documents, and alias
 * expansion beyond those bounds are rejected. Quoted scalars remain strings;
 * plain true/false/null and finite JSON-style numbers become typed JSON values.
 *
 * @param[in] text Complete UTF-8 YAML document.
 * @return Decoded JSON tree; application-specific schema validation is separate.
 * @throws std::runtime_error For invalid YAML, unsupported structure, parser setup
 *                            failure, or size/depth/expansion-limit violations.
 * @note Does not modify parser backends, configuration snapshots, or external state.
 */
json readYaml(const std::string& text);
/**
 * @brief Read and decode a single bounded YAML configuration file.
 *
 * Checks the file length before allocating its input buffer and rejects reads
 * whose length changes. Content is then subject to readYaml() document limits.
 *
 * @param[in] path Configuration path, relative to the working directory or absolute.
 * @return Decoded JSON tree, without application-specific policy validation.
 * @throws std::runtime_error If reading fails, the length changes, or decoding fails.
 * @note Reads the filesystem but does not update an active configuration.
 */
json readYamlFile(const std::string& path);
/**
 * @brief Return the build-configured default configuration path.
 *
 * Uses SMARTHOME_PHRASE_FILE when supplied by the build; otherwise returns
 * "config/phrases.yaml" relative to the application's working directory.
 *
 * @return Default configuration filename; existence is not checked.
 */
std::string defaultFile();

class Configuration {
    std::string path;
    std::string seen;
    std::string active;
    std::string failure;
    json values;
    std::function<void(const json&)> apply;

    /**
     * @brief Prepare a decoded candidate and atomically replace the stored snapshot.
     *
     * Decodes the candidate and invokes the application callback before committing
     * values and accepted source text. Commit uses swaps after callback success.
     *
     * @param[in] text Candidate YAML source, owned so its storage can become active.
     * @throws std::exception If decoding or the application callback fails. Stored
     *                       values and active source remain unchanged.
     * @note Callback implementations must provide their own strong exception guarantee
     *       and avoid external effects that cannot be rolled back.
     */
    void load(std::string text);

public:
    /**
     * @brief Construct a file-backed configuration and perform its mandatory first load.
     *
     * A successful constructor leaves both the stored snapshot and the callback's
     * consumer configured. Missing or invalid initial configuration is a startup error.
     *
     * @param[in] path File to read now and on subsequent reloads.
     * @param[in] apply Callable that validates/prepares consumers before each commit.
     *                  Captured consumers must outlive this Configuration instance.
     * @throws std::exception If file reading, decoding, or initial application fails.
     * @note Reads a file and calls apply during construction; performs no logging or
     *       device-control requests itself.
     */
    Configuration(std::string path, std::function<void(const json&)> apply);
    /**
     * @brief Disable copying of file state and the consumer-bound application callback.
     *
     * A configuration controller has one identity; copying it could duplicate reload
     * state while retaining references to the same consumers.
     */
    Configuration(const Configuration&) = delete;
    /**
     * @brief Disable assignment that would replace a controller's bound consumers.
     *
     * The configuration path, reload history, and callback remain associated with the
     * instance established at construction.
     */
    Configuration& operator=(const Configuration&) = delete;
    /**
     * @brief Force a fresh read, validation, and application of the configured file.
     *
     * Reapplies valid content even when it is byte-for-byte unchanged, replacing
     * consumer-local additions with the file-backed state and clearing prior errors.
     *
     * @throws std::exception If reading, decoding, or applying fails; the prior stored
     *                       snapshot remains active. This method propagates the error.
     * @note Performs filesystem access and invokes the configured callback.
     */
    void reload();
    /**
     * @brief Poll the file and apply content that differs from the last attempted text.
     *
     * Repeated invalid content is not recompiled. A restored active file clears a
     * prior read error, while rejected replacement content leaves the active snapshot
     * intact. Exceptions from reading, decoding, or applying are captured in error().
     *
     * @return True only when a new candidate is accepted; false for unchanged or failed
     *         attempts. Consult error() to distinguish a failure from unchanged content.
     * @note Reads the file on every call; invokes the application callback only for a
     *       candidate requiring a new attempt. Not synchronized for concurrent use.
     */
    bool reloadIfChanged();
    /**
     * @brief Access the currently accepted decoded configuration.
     *
     * @return Borrowed reference to active values. Copy it when a stable snapshot must
     *         survive a later successful reload or destruction of this instance.
     * @note Does not read the file, reload content, or invoke the application callback.
     */
    const json& data() const;
    /**
     * @brief Inspect the error from the latest failed polling attempt.
     *
     * @return Borrowed error string, empty after successful load or recognition of the
     *         restored active file. Repeated rejected content retains its prior error.
     * @note Does not perform I/O or clear the error when it is read.
     */
    const std::string& error() const;
};
} // namespace Config
