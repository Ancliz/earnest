/**
 * @file
 * @brief Audio playback through PortAudio.
 */

#pragma once

#include "audio/audio.hpp"

namespace Audio {
/**
 * @brief Plays buffers on the default output device, blocking until each finishes.
 */
class PortAudioPlayer final : public Player {
public:
    /**
     * @brief Initialise PortAudio.
     * @throws std::runtime_error If PortAudio cannot start.
     */
    PortAudioPlayer();

    /**
     * @brief Release PortAudio.
     */
    ~PortAudioPlayer() override;

    PortAudioPlayer(const PortAudioPlayer&) = delete;
    PortAudioPlayer& operator=(const PortAudioPlayer&) = delete;

    /**
     * @brief Play the buffer; an empty one plays nothing.
     * @throws std::runtime_error If the buffer is invalid or the device reports an error.
     */
    void play(const Buffer& buffer) override;
};
} // namespace Audio