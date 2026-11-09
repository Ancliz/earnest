#pragma once

#include <chrono>
#include <string_view>
#include <spdlog/spdlog.h>
#include "audio/audio.hpp"

using std::string_view;
using Audio::Buffer;
using Audio::Player;

namespace tts {

class Engine {

public:

    virtual ~Engine() = default;

    virtual Buffer generate(string_view text) = 0;
};

class SpeechService {

private:
    Engine& engine;
    Player& player_;

public:

    SpeechService(Engine& engine, Player& player) : engine(engine), player_(player) {}

    void speak(string_view text) {
        using Clock = std::chrono::steady_clock;
        using std::chrono::duration_cast;
        using std::chrono::milliseconds;

        spdlog::info("Speaking: {}", text);
        const auto started = Clock::now();
        const auto audio = engine.generate(text);
        const auto generated = Clock::now();
        const auto frames = audio.channels ? audio.samples.size() / audio.channels : 0;
        spdlog::info("Speech generated in {} ms: {} frames, {} Hz, {} channel(s)", duration_cast<milliseconds>(generated - started).count(), frames, audio.sampleRate, audio.channels);
        player_.play(audio);
        spdlog::info("Speech played in {} ms", duration_cast<milliseconds>(Clock::now() - generated).count());
    }
};
}