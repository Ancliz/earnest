#pragma once

#include "audio.hpp"
#include "app/settings.hpp"
#include <deque>
#include <optional>

class UtteranceBuffer {
    Audio::Samples audio;
    std::deque<Audio::Samples> preroll;
    int voicedBlocks = 0;
    int silentBlocks = 0;
    int silenceBlocks;
    float threshold;
    bool discardUntilSilence = false;

    void clear();

public:
    explicit UtteranceBuffer(const Settings& settings);
    std::optional<Audio::Samples> finish();
    std::optional<Audio::Samples> append(const Audio::Samples& block, bool discontinuity);
};