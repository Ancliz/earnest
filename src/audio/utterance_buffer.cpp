/**
 * @file
 * @brief Preroll, silence boundaries, and incomplete-utterance recovery.
 */

#include "utterance_buffer.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

using Audio::sampleRate;
using Audio::Samples;
using std::nullopt;
using std::optional;

namespace {
/// How far above the background a block must be to count as speech.
constexpr double backgroundMargin = 1.5;

/// How much of the background remains after each block well below it.
constexpr double backgroundFade = 0.98;

/**
 * @brief Root-mean-square level of a block; zero for an empty block.
 */
double rms(const Samples& block) {
    double energy = 0;

    for(float sample : block) {
        energy += sample * sample;
    }

    return block.empty() ? 0 : std::sqrt(energy / block.size());
}
} // namespace

void UtteranceBuffer::reset() {
    audio.clear();
    preroll.clear();
    voicedBlocks = trailingSilentBlocks = 0;
}

UtteranceBuffer::UtteranceBuffer(float threshold, int silenceMs)
    : blocksToEndUtterance((silenceMs + 99) / 100), threshold(threshold) {
    if(!std::isfinite(threshold) || threshold <= 0 || threshold >= 1 || silenceMs < 200 || silenceMs > 3000) {
        throw std::invalid_argument("Invalid utterance threshold or silence duration");
    }
}

void UtteranceBuffer::abandon() {
    reset();
    discardUntilSilence = true;
}

optional<Samples> UtteranceBuffer::finish() {
    optional<Samples> completed;

    if(!discardUntilSilence && voicedBlocks >= 2) {
        completed = std::move(audio);
    }

    reset();
    return completed;
}

optional<Samples> UtteranceBuffer::append(const Samples& block, bool discontinuity) {
    lastEvent = Event::None;

    if(discontinuity) {
        lastEvent = Event::Discontinuity;
        reset();
        discardUntilSilence = true;
    }

    const double level = rms(block);
    const bool speech = !block.empty() && level >= std::max<double>(threshold, background * backgroundMargin);

    if(level < background / 2) {
        background *= backgroundFade;
    }

    if(discardUntilSilence) {
        trailingSilentBlocks = speech ? 0 : trailingSilentBlocks + 1;

        if(trailingSilentBlocks >= blocksToEndUtterance) {
            discardUntilSilence = false;
            trailingSilentBlocks = 0;
        }

        return nullopt;
    }

    if(audio.empty() && !speech) {
        preroll.push_back(block);

        if(preroll.size() > 2) {
            preroll.pop_front();
        }

        return nullopt;
    }

    // Speech starts: prepend the quiet lead-in so the first syllable is not clipped.
    if(audio.empty()) {
        for(const auto& previous : preroll) {
            audio.insert(audio.end(), previous.begin(), previous.end());
        }

        preroll.clear();
    }

    audio.insert(audio.end(), block.begin(), block.end());
    voicedBlocks += speech;
    trailingSilentBlocks = speech ? 0 : trailingSilentBlocks + 1;

    if(trailingSilentBlocks >= blocksToEndUtterance) {
        return finish();
    }

    if(audio.size() >= 15 * sampleRate) {
        lastEvent = Event::TooLong;
        background = rms(audio);
        reset();
        discardUntilSilence = true;
    }

    return nullopt;
}