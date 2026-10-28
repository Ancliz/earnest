/**
 * @file
 * @brief Ownership and synchronization contract for bounded PortAudio capture.
 */

#pragma once

#include "audio.hpp"
#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <portaudio.h>

class Microphone {
    PaStream* stream = nullptr;
    std::thread worker;
    std::atomic<bool> stopping{false};
    std::mutex audioMutex;
    std::condition_variable ready;
    std::deque<Audio::Samples> pending;
    PaError failure = paNoError;
    bool lostAudio = false;

    /**
     * @brief Read fixed-size audio blocks on the owned capture thread.
     *
     * Queues 100 ms blocks under the audio mutex and wakes waiting readers. A device
     * overflow or a queue of 50 blocks discards queued audio and marks a discontinuity.
     * Other PortAudio read failures are stored for the next read() call to report.
     *
     * @pre The PortAudio stream is open and started; called only by the owned worker.
     * @note Performs blocking device reads and changes the queue/failure flags. It does
     *       not transcribe, log, or execute commands.
     */
    void capture();

public:
    /**
     * @brief Open the default microphone and begin bounded background capture.
     *
     * Initializes PortAudio, verifies mono float32 input at Audio::sampleRate, opens
     * and starts the stream, then launches the capture worker. Partial setup is cleaned
     * up if a checked device operation or worker creation fails.
     *
     * @throws std::runtime_error For absent/unusable input devices or PortAudio failures.
     * @throws std::system_error If the capture thread cannot be created.
     * @note Acquires audio resources immediately and starts recording during construction.
     */
    Microphone();
    /**
     * @brief Stop capture and release the worker, stream, and PortAudio resources.
     *
     * Signals shutdown, aborts the stream before joining the worker, closes the stream,
     * and terminates this instance's PortAudio use. Pending samples are discarded.
     *
     * @note Blocks until the worker exits; PortAudio cleanup return codes are ignored.
     */
    ~Microphone();
    /**
     * @brief Disable copying of a live stream and its capture thread.
     *
     * Each microphone instance exclusively owns its stream, queue, and synchronization state.
     */
    Microphone(const Microphone&) = delete;
    /**
     * @brief Disable assignment of exclusively owned capture resources.
     *
     * Create a separate microphone instance to acquire another stream.
     */
    Microphone& operator=(const Microphone&) = delete;
    /**
     * @brief Wait briefly for the next captured audio block and transfer it to the caller.
     *
     * Waits up to 100 ms for queued audio or a stored capture error. Successfully
     * returned audio is removed from the queue; a pending discontinuity is reported
     * once and cleared when that block is delivered.
     *
     * @param[out] block Receives the oldest queued mono float32 block on success;
     *                   unchanged when no block is available.
     * @param[out] discontinuity True if audio was lost since the previous delivery;
     *                           unchanged when no block is returned.
     * @return True when one block was delivered, false after a wait with no queued audio.
     * @throws std::runtime_error If the capture worker stored a PortAudio read failure.
     * @note Synchronizes queue access and consumes data; performs no transcription or HTTP.
     */
    bool read(Audio::Samples& block, bool& discontinuity);
};