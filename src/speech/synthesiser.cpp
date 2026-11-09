/**
 * @file
 * @brief Generating and playing speech, with timing logs.
 */

#include "synthesiser.hpp"

#include <chrono>
#include <spdlog/spdlog.h>

using Clock = std::chrono::steady_clock;
using std::chrono::duration_cast;
using std::chrono::milliseconds;

namespace Speech {
void SpeechService::speak(string_view text) {
    spdlog::info("Speaking: {}", text);
    const auto started = Clock::now();
    const auto audio = synthesiser.generate(text);
    const auto generated = Clock::now();
    const auto frames = audio.channels ? audio.samples.size() / audio.channels : 0;
    spdlog::info("Speech generated in {} ms: {} frames, {} Hz, {} channel(s)", duration_cast<milliseconds>(generated - started).count(), frames, audio.sampleRate, audio.channels);
    player.play(audio);
    spdlog::info("Speech played in {} ms", duration_cast<milliseconds>(Clock::now() - generated).count());
}
} // namespace Speech