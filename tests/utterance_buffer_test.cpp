/**
 * @file
 * @brief Regression suite for utterance buffer behavior.
 */

#include "audio/utterance_buffer.hpp"

#include <iostream>
#include <stdexcept>

using Audio::blockSamples;
using Audio::Samples;

/**
 * @brief Exercise utterance completion, preroll, noise filtering, and discard recovery.
 *
 * Feeds synthetic 100 ms quiet/loud blocks through segmentation, checks explicit
 * shutdown flushing, and verifies that discontinuities and overlong speech never
 * emit incomplete commands.
 *
 * @return Zero when every assertion completes.
 * @throws std::runtime_error If a buffering expectation fails.
 * @note Uses no microphone, model, configuration file, or network service.
 */
int main() {
    /*
     * @brief Raise a descriptive assertion failure for the synthetic audio fixture.
     *
     * @param[in] valid Whether the expected segmentation behavior occurred.
     * @param[in] description Assertion description used as the exception message.
     * @throws std::runtime_error If valid is false.
     */
    const auto check = [](bool valid, const char* description) {
        if(!valid) {
            throw std::runtime_error(description);
        }
    };
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
