#include "utterance_buffer.hpp"

#include <cmath>
#include <iostream>
#include <utility>

using Audio::sampleRate;
using Audio::Samples;
using std::optional;

void UtteranceBuffer::clear() {
    audio.clear();
    preroll.clear();
    voicedBlocks = silentBlocks = 0;
}

UtteranceBuffer::UtteranceBuffer(const Settings& settings)
    : silenceBlocks((settings.silenceMs + 99) / 100), threshold(settings.threshold) {}

optional<Samples> UtteranceBuffer::finish() {
    optional<Samples> completed;

    if(!discardUntilSilence && voicedBlocks >= 2) {
        completed = std::move(audio);
    }

    clear();
    return completed;
}

optional<Samples> UtteranceBuffer::append(const Samples& block, bool discontinuity) {
    if(discontinuity) {
        std::cerr << "Microphone overflow: discarding incomplete command\n";
        clear();
        discardUntilSilence = true;
    }

    double energy = 0;

    for(float sample : block) {
        energy += sample * sample;
    }

    const bool speech = !block.empty() && std::sqrt(energy / block.size()) >= threshold;

    if(discardUntilSilence) {
        silentBlocks = speech ? 0 : silentBlocks + 1;

        if(silentBlocks >= silenceBlocks) {
            discardUntilSilence = false;
            silentBlocks = 0;
        }

        return std::nullopt;
    }

    if(audio.empty() && !speech) {
        preroll.push_back(block);

        if(preroll.size() > 2) {
            preroll.pop_front();
        }

        return std::nullopt;
    }

    if(audio.empty()) {
        for(const auto& previous : preroll) {
            audio.insert(audio.end(), previous.begin(), previous.end());
        }

        preroll.clear();
    }

    audio.insert(audio.end(), block.begin(), block.end());
    voicedBlocks += speech;
    silentBlocks = speech ? 0 : silentBlocks + 1;

    if(silentBlocks >= silenceBlocks) {
        return finish();
    }

    if(audio.size() >= 15 * sampleRate) {
        std::cerr << "Utterance exceeded 15 seconds; waiting for silence\n";
        clear();
        discardUntilSilence = true;
    }

    return std::nullopt;
}