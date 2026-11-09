/**
 * @file
 * @brief Spotify executor that talks to the local Spotify control server rather than Spotify itself.
 */

#pragma once

#include "commands/command.hpp"

#include <httplib.h>

using namespace Commands;

/**
 * @brief Runs spotify-domain commands through the Spotify control server.
 *
 * Playback controls such as play, pause and volume stay with Home Assistant's media players.
 * This executor adds what those lack:
 *
 * - cast: move playback to a Spotify Connect device (the command's entity).
 * - get_queue: what is playing and up next.
 * - request_song: queue the best match for {"song": ...}.
 * - time_until: how long until a queued {"song": ...} plays.
 *
 * Spotify Connect devices are discovered as entities "spotify.<device id>". The server does the
 * work (the Spotify login, searching, matching songs against the queue and timing them); this
 * executor validates commands, calls the matching endpoint and words the reply.
 */
class Spotify final : public Executor {

    /// Connection to the Spotify control server.
    httplib::Client client;

    /**
     * @brief GET a JSON response from the server.
     * @throws std::runtime_error For transport failures, non-200 statuses, or invalid JSON.
     */
    json get(const string& path, const httplib::Params& params = {});

    /**
     * @brief Send a JSON body to the server and return its JSON response, or null when it has none.
     * @throws std::runtime_error For transport failures, non-2xx statuses, or invalid JSON.
     */
    json send(const string& method, const string& path, const json& body);

    /**
     * @brief Whether a device is, or within about 1.5 seconds becomes, the active one.
     */
    bool becomesActive(const string& deviceId);

    /**
     * @brief Move playback to a device and start playing there.
     * @throws std::runtime_error If the transfer fails and the device does not become active.
     */
    void cast(const string& deviceId);

    /**
     * @brief Say what is playing and the next few tracks.
     * @throws std::runtime_error If the server fails.
     */
    string describeQueue();

    /**
     * @brief Queue the best match for a song, and say which track that was.
     * @throws std::runtime_error If the server fails.
     */
    string requestSong(const string& song);

    /**
     * @brief Say how long until a queued song plays, or that it is not queued.
     * @throws std::runtime_error If the server fails or gives no time.
     */
    string timeUntil(const string& song);

public:
    /**
     * @brief Configure the server origin without making a request.
     * @param[in] url Origin including scheme and port, such as "http://127.0.0.1:8888".
     */
    explicit Spotify(const string& url);

    /**
     * @brief Nothing in the phrase files applies to Spotify.
     */
    void configure(const json&) override {}

    /**
     * @brief Spotify Connect devices that playback can be cast to.
     * @throws std::runtime_error If the server cannot be reached.
     */
    Devices discoverDevices() override;

    /**
     * @brief The spotify-domain services this executor runs; fixed, so no request is made.
     */
    ServiceCatalogue discoverServices() override;

    /**
     * @brief Check the service, its fields and, for cast, that the device is available.
     */
    Result preview(const Command& command) override;

    /**
     * @brief Preview, then run the command once; information requests fill Result::reply.
     */
    Result execute(const Command& command) override;
};