/**
 * @file
 * @brief Energy-based segmentation and discard-event reporting contracts.
 */

#pragma once

#include "audio.hpp"
#include <deque>
#include <optional>

class UtteranceBuffer {
public:
    enum class Event { None, Discontinuity, TooLong };

private:
    Audio::Samples audio;
    std::deque<Audio::Samples> preroll;
    int voicedBlocks = 0;
    int silentBlocks = 0;
    int silenceBlocks;
    float threshold;
    bool discardUntilSilence = false;

    /**
     * @brief Reset accumulated audio, preroll, and speech/silence block counters.
     *
     * Does not change the discard-until-silence state, configured thresholds, or last
     * append event; callers manage those state transitions explicitly.
     */
    void clear();

    Event lastEvent = Event::None;

public:
    /**
     * @brief Configure energy-based utterance segmentation without opening an audio device.
     *
     * Rounds the requested silence duration up to whole 100 ms blocks. Detection
     * requires at least two voiced blocks and retains up to two leading quiet blocks.
     *
     * @param[in] threshold Finite RMS speech threshold strictly between zero and one.
     * @param[in] silenceMs Silence boundary duration in milliseconds, within 200..3000.
     * @throws std::invalid_argument For an invalid threshold or silence duration.
     * @note Expects subsequent append() calls to represent 100 ms audio blocks.
     */
    UtteranceBuffer(float threshold, int silenceMs);

    /**
     * @brief Inspect the discard event reported by the most recent append() call.
     *
     * @return None, Discontinuity, or TooLong. Reading does not consume the event;
     *         append() resets it before processing its next block.
     * @note clear() and finish() do not reset the last append event.
     */
    Event event() const {
        return lastEvent;
    }

    /**
     * @brief Complete buffered speech, including an explicit shutdown flush.
     *
     * Emits owned audio only after at least two voiced blocks and only when the
     * buffer is not discarding incomplete speech. Always clears accumulated audio,
     * preroll, and counters; a repeated call therefore cannot emit the same audio.
     *
     * @return Completed samples, or no value for silence, brief noise, or discarded audio.
     * @note Moves audio out of this instance; does not transcribe, log, or clear an
     *       outstanding discard-until-silence condition.
     */
    std::optional<Audio::Samples> finish();
    /**
     * @brief Append one audio block and emit an utterance when its silence boundary arrives.
     *
     * Computes RMS energy, preserves leading/trailing audio, and tracks voiced and
     * silent block counts. A discontinuity discards incomplete input; a 15-second
     * utterance also enters discard mode. Both require a new silence boundary before
     * speech can be accepted again. The caller can inspect event() for diagnostics.
     *
     * @param[in] block Next nominal 100 ms mono float32 block; copied into owned buffers.
     * @param[in] discontinuity Whether capture lost audio before this block.
     * @return Completed utterance at a silence boundary, or no value while accumulating,
     *         filtering brief noise, or recovering from discarded input.
     * @note Mutates buffering state only. Does not read devices, print, or run inference.
     */
    std::optional<Audio::Samples> append(const Audio::Samples& block, bool discontinuity);
};