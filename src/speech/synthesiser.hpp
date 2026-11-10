/**
 * @file
 * @brief Text-to-speech interface and the service that voices text.
 */

#pragma once

#include "audio/audio.hpp"

#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <string_view>
#include <thread>

namespace Speech {
using Audio::Buffer;
using Audio::Player;
using std::string_view;

/**
 * @brief Turns text into audio.
 */
class Synthesiser {
public:
    /**
     * @brief Destroy a synthesiser through the interface.
     */
    virtual ~Synthesiser() = default;

    /**
     * @brief Generate speech for the text.
     * @throws std::runtime_error If speech cannot be generated.
     */
    virtual Buffer generate(string_view text) = 0;
};

/**
 * @brief Generates speech on the caller's thread and plays it in order on its own.
 *
 * The next sentence can be generated while the last one plays, so a long reply has no gaps.
 */
class SpeechService {

    /// Generates the audio; not owned.
    Synthesiser& synthesiser;

    /// Plays the audio on the playback thread only; not owned.
    Player& player;

    /// Guards waiting and stopping.
    std::mutex queueMutex;

    /// Signalled when audio is queued or stopping is set.
    std::condition_variable ready;

    /// Generated audio not yet started.
    std::deque<Buffer> waiting;

    /// Set by the destructor; the thread plays what is left and returns.
    bool stopping = false;

    /// Audio queued or playing.
    std::atomic<std::size_t> unfinished{0};

    /// Thread running playAll(); declared last so it starts after every other member exists.
    std::thread playback;

    /**
     * @brief Play queued audio in order until stopping and the queue is empty; runs on the playback thread.
     */
    void playAll();

public:
    /**
     * @brief Voice text with the given synthesiser and player.
     * @throws std::system_error If the thread cannot start.
     */
    SpeechService(Synthesiser& synthesiser, Player& player);

    /**
     * @brief Play everything still queued, then stop the thread.
     */
    ~SpeechService();

    /**
     * @brief Copying is disabled; each instance owns its thread.
     */
    SpeechService(const SpeechService&) = delete;

    /**
     * @brief Assignment is disabled; each instance owns its thread.
     */
    SpeechService& operator=(const SpeechService&) = delete;

    /**
     * @brief Generate the text's speech and queue it, returning without waiting for it to play.
     * @throws std::runtime_error If speech cannot be generated; a playback failure is only logged.
     */
    void speak(string_view text);

    /**
     * @brief Whether any speech is queued or playing; safe from any thread.
     */
    bool isBusy() const { return unfinished > 0; }
};
} // namespace Speech