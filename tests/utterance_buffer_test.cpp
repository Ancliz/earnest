/**
 * @file
 * @brief Regression suite for utterance buffering.
 */

#include "check.hpp"
#include "audio/utterance_buffer.hpp"

#include <iostream>

using Audio::blockSamples;
using Audio::Samples;

/**
 * @brief Exercise completion, preroll, noise filtering, discard recovery, and discard events.
 */
int main() {
    UtteranceBuffer buffer(0.01f, 200);
    const Samples quiet(blockSamples, 0);
    const Samples loud(blockSamples, 0.1f);
    check(!buffer.append(quiet, false), "silence is not speech");
    check(!buffer.append(loud, false), "first speech block waits");
    check(!buffer.append(loud, false), "speech waits for silence");
    check(!buffer.append(quiet, false), "one silence block waits");
    const auto completed = buffer.append(quiet, false);
    check(completed && completed->size() == 5 * blockSamples, "preserves leading and trailing audio");
    check(!buffer.finish(), "does not emit twice");
    buffer.append(loud, false);
    buffer.append(quiet, false);
    check(!buffer.append(quiet, false), "brief noise is ignored");
    buffer.append(loud, false);
    buffer.append(loud, false);
    check(buffer.finish().has_value(), "shutdown flush");
    buffer.append(loud, false);
    buffer.append(loud, true);
    check(buffer.event() == UtteranceBuffer::Event::Discontinuity, "a capture gap is reported");
    check(!buffer.finish(), "overflow discards partial command");
    buffer.append(quiet, false);
    buffer.append(quiet, false);
    buffer.append(loud, false);
    buffer.append(loud, false);
    check(buffer.finish().has_value(), "overflow recovers after silence");

    for(int i = 0; i < 150; ++i) {
        check(!buffer.append(loud, false), "long utterance never emits partial speech");
    }

    check(buffer.event() == UtteranceBuffer::Event::TooLong, "an overlong utterance is reported");
    check(!buffer.finish(), "long utterance is discarded on shutdown");

    UtteranceBuffer room(0.01f, 200);
    const Samples music(blockSamples, 0.05f);
    const Samples voice(blockSamples, 0.2f);
    const Samples soft(blockSamples, 0.02f);

    for(int i = 0; i < 150; ++i) {
        room.append(music, false);
    }

    check(room.event() == UtteranceBuffer::Event::TooLong, "steady sound is first discarded as an overlong utterance");
    room.append(music, false);
    room.append(music, false);
    check(!room.finish(), "steady sound then counts as background");
    room.append(voice, false);
    room.append(voice, false);
    room.append(music, false);
    check(room.append(music, false).has_value(), "speech above the background is heard");
    room.append(soft, false);
    room.append(soft, false);
    check(!room.finish(), "softer speech does not rise above the background");

    for(int i = 0; i < 100; ++i) {
        room.append(quiet, false);
    }

    room.append(soft, false);
    room.append(soft, false);
    check(room.finish().has_value(), "the background fades once the room is quiet");
    std::cout << checks << " utterance buffer checks passed\n";
}