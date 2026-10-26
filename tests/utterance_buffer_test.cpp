#include "audio/utterance_buffer.hpp"

#include <iostream>
#include <stdexcept>

using Audio::blockSamples;
using Audio::Samples;

int main() {
    const auto check = [](bool valid, const char* description) {
        if(!valid) {
            throw std::runtime_error(description);
        }
    };
    Settings settings;
    settings.silenceMs = 200;
    UtteranceBuffer buffer(settings);
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
    check(!buffer.finish(), "overflow discards partial command");
    buffer.append(quiet, false);
    buffer.append(quiet, false);
    buffer.append(loud, false);
    buffer.append(loud, false);
    check(buffer.finish().has_value(), "overflow recovers after silence");

    for(int i = 0; i < 150; ++i) {
        check(!buffer.append(loud, false), "long utterance never emits partial speech");
    }

    check(!buffer.finish(), "long utterance is discarded on shutdown");
    std::cout << "Utterance buffering and shutdown checks passed\n";
}
