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
SpeechService::SpeechService(Synthesiser& synthesiser, Player& player) : synthesiser(synthesiser), player(player), playback(&SpeechService::playAll, this) {}

SpeechService::~SpeechService() {
    {
        std::lock_guard<std::mutex> lock(queueMutex);
        stopping = true;
    }

    ready.notify_one();
    playback.join();
}

void SpeechService::speak(string_view text) {
    spdlog::info("Speaking: {}", text);
    const auto started = Clock::now();
    auto audio = synthesiser.generate(text);
    const auto frames = audio.channels ? audio.samples.size() / audio.channels : 0;
    spdlog::info("Speech generated in {} ms: {} frames, {} Hz, {} channel(s)", duration_cast<milliseconds>(Clock::now() - started).count(), frames, audio.sampleRate, audio.channels);
    ++unfinished;

    {
        std::lock_guard<std::mutex> lock(queueMutex);
        waiting.push_back(std::move(audio));
    }

    ready.notify_one();
}

void SpeechService::interrupt() {
    std::lock_guard<std::mutex> lock(queueMutex);
    unfinished -= waiting.size();
    waiting.clear();
    cancelled = true;

    if(unfinished == 0) {
        lastFinished = Clock::now().time_since_epoch().count();
    }
}

void SpeechService::playAll() {
    while(true) {
        std::unique_lock<std::mutex> lock(queueMutex);
        ready.wait(lock, [&] { return stopping || !waiting.empty(); });

        if(waiting.empty()) {
            return;
        }

        const auto audio = std::move(waiting.front());
        waiting.pop_front();

        // Cleared while locked, so an interruption either drops this audio or cuts it short.
        cancelled = false;
        lock.unlock();
        const auto started = Clock::now();

        // One failed sentence must not end the thread and silence every later reply.
        try {
            player.play(audio, cancelled);
            spdlog::info("Speech played in {} ms", duration_cast<milliseconds>(Clock::now() - started).count());
        } catch(const std::exception& error) {
            spdlog::warn("Cannot play speech: {}", error.what());
        }

        if(--unfinished == 0) {
            lastFinished = Clock::now().time_since_epoch().count();
        }
    }
}
} // namespace Speech