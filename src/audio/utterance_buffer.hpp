/**
 * @file
 * @brief Energy-based utterance segmentation.
 */

#pragma once

#include "audio.hpp"
#include <deque>
#include <optional>

using Audio::Samples;

/**
 * @brief Groups 100 ms audio blocks into utterances separated by silence.
 *
 * A block is speech when its RMS reaches the threshold. An utterance needs at least two speech
 * blocks, keeps up to two quiet lead-in blocks, and ends after the configured silence. Audio
 * after a capture gap or beyond 15 seconds is discarded until the next full silence. Sound that
 * lasts 15 seconds is treated as background, such as music, and speech must then rise above it;
 * the background fades once the room quietens.
 */
class UtteranceBuffer {
public:
    /**
     * @brief Why the latest append() discarded audio.
     */
    enum class Event {
        /// Nothing was discarded.
        None,
        /// Capture lost audio, so the incomplete utterance was dropped.
        Discontinuity,
        /// The utterance passed 15 seconds and was dropped.
        TooLong
    };

private:
    /// Samples of the utterance in progress.
    Samples audio;

    /// Quiet blocks kept so the start of speech is not clipped.
    std::deque<Samples> preroll;

    /// Speech blocks in the current utterance.
    int voicedBlocks = 0;

    /// Consecutive quiet blocks at the end of the buffered audio.
    int trailingSilentBlocks = 0;
    
    /// Quiet blocks that end an utterance.
    int blocksToEndUtterance;

    /// RMS level treated as speech.
    float threshold;

    /// RMS level of steady background sound; zero in a quiet room.
    double background = 0;

    /// Whether audio is ignored until the next full silence.
    bool discardUntilSilence = false;
    
    /// Discard reported by the latest append().
    Event lastEvent = Event::None;

    /**
     * @brief Drop buffered audio and block counts; discard mode and the last event are kept.
     */
    void reset();

public:
    /**
     * @brief Configure segmentation.
     * @param[in] threshold RMS speech level, strictly between 0 and 1.
     * @param[in] silenceMs Silence that ends an utterance, 200..3000 ms, rounded up to whole blocks.
     * @throws std::invalid_argument For an out-of-range threshold or silence.
     */
    UtteranceBuffer(float threshold, int silenceMs);

    /**
     * @brief Discard reported by the latest append().
     */
    Event event() const {
        return lastEvent;
    }

    /**
     * @brief Emit the buffered utterance now, as on shutdown, and clear the buffer.
     * @return Utterance, or nullopt for silence, brief noise, or discarded audio.
     */
    std::optional<Samples> finish();

    /**
     * @brief Add one 100 ms block.
     * @param[in] discontinuity Whether capture lost audio before this block.
     * @return The completed utterance when this block ends it, otherwise nullopt.
     */
    std::optional<Samples> append(const Samples& block, bool discontinuity);
};