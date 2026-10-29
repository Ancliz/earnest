/**
 * @file
 * @brief Default-microphone capture on a background thread with a bounded queue.
 */

#pragma once

#include "audio.hpp"
#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <portaudio.h>

/**
 * @brief Records the default microphone in 100 ms blocks while the caller is busy elsewhere.
 *
 * The queue holds at most five seconds; overflow drops the queued audio and reports a
 * discontinuity on the next read.
 */
class Microphone {
    
    /// Open PortAudio input stream.
    PaStream* stream = nullptr;

    /// Capture thread running capture().
    std::thread worker;

    /// Set to stop the capture thread.
    std::atomic<bool> stopping{false};

    /// Guards pending, failure and lostAudio.
    std::mutex audioMutex;

    /// Signalled when a block or failure is queued.
    std::condition_variable ready;

    /// Captured blocks not yet read.
    std::deque<Audio::Samples> pending;

    /// Read error reported by the capture thread.
    PaError failure = paNoError;

    /// Whether audio was dropped since the last delivered block.
    bool lostAudio = false;

    /**
     * @brief Read blocks into the queue until stopped or a read fails; runs on the worker thread.
     */
    void capture();

public:
    /**
     * @brief Open the default microphone as mono float32 at Audio::sampleRate and start capturing.
     * @throws std::runtime_error For a missing or unusable input device or a PortAudio failure.
     * @throws std::system_error If the capture thread cannot start.
     */
    Microphone();

    /**
     * @brief Stop capture and release the stream; queued audio is discarded.
     */
    ~Microphone();

    /**
     * @brief Copying is disabled; each instance owns its stream and thread.
     */
    Microphone(const Microphone&) = delete;

    /**
     * @brief Assignment is disabled; each instance owns its stream and thread.
     */
    Microphone& operator=(const Microphone&) = delete;

    /**
     * @brief Wait up to 100 ms for the next block.
     * @param[out] block Oldest queued block, when one is returned.
     * @param[out] discontinuity Whether audio was lost before that block, when one is returned.
     * @return Whether a block was returned.
     * @throws std::runtime_error If the capture thread hit a read error.
     */
    bool read(Audio::Samples& block, bool& discontinuity);

    /**
     * @brief Drop queued audio and any pending overflow, so reading resumes with live audio.
     *
     * For use after a long pause in reading, such as executing a command, when the queued audio
     * is stale and may have overflowed.
     */
    void discardPending();
};