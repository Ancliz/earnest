/**
 * @file
 * @brief Text-to-speech interface and the service that voices text.
 */

#pragma once

#include "audio/audio.hpp"

#include <string_view>

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
 * @brief Generates speech and plays it, blocking until playback finishes.
 */
class SpeechService {

    /// Generates the audio; not owned.
    Synthesiser& synthesiser;

    /// Plays the audio; not owned.
    Player& player;

public:
    /**
     * @brief Voice text with the given synthesiser and player.
     */
    SpeechService(Synthesiser& synthesiser, Player& player) : synthesiser(synthesiser), player(player) {}

    /**
     * @brief Say the text.
     * @throws std::runtime_error If speech cannot be generated or played.
     */
    void speak(string_view text);
};
} // namespace Speech