/**
 * @file
 * @brief Background thread that turns captured utterances into executed commands.
 */

#pragma once

#include "commands/command.hpp"
#include "config/configuration.hpp"
#include "speech/transcriber.hpp"
#include "speech/synthesiser.hpp"

#include <condition_variable>
#include <deque>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

using Audio::Samples;
using std::string;

/**
 * @brief Log the command parsed from the text, or that none was recognized.
 *
 * Log the input itself before parsing, so it precedes anything the parser logs.
 */
void logParsed(const std::optional<Commands::Command>& command);

/**
 * @brief Log a preview or execution outcome.
 * @param[in] preview Whether the result came from preview() rather than execute().
 * @return Whether the command was accepted.
 */
bool logResult(const Commands::Result& result, bool preview);

/**
 * @brief Transcribes, parses and executes utterances on its own thread, in the order submitted.
 *
 * The capture loop only submits audio, so it keeps listening while a command runs. The
 * transcriber, parser, executor and configuration are used from the worker thread alone, so they
 * need no locking; the caller must not use them while the worker exists. The phrase file is
 * reloaded every 5 seconds while idle and before each command.
 */
class CommandWorker {

    /// Utterances waiting beyond this are dropped, oldest first, so commands never run stale.
    static constexpr std::size_t maxPending = 3;

    /// Speech recognizer, used on the worker thread only.
    Speech::Transcriber& transcriber;

    /// Says the outcome of each command, used on the worker thread only.
    Speech::SpeechService& speech;

    /// Command parser, used on the worker thread only.
    Commands::Parser& parser;

    /// Device discovery and command execution, used on the worker thread only.
    Commands::Executor& executor;

    /// Phrase file applied to the parser and executor, reloaded on the worker thread only.
    Config::Configuration& configuration;

    /// Phrase that must open each command; empty accepts every utterance.
    string wakeWord;

    /// Last reload error reported, so a broken file is reported once rather than on every check.
    string lastReloadError;

    /// Guards pending and stopping.
    std::mutex queueMutex;

    /// Signalled when an utterance is submitted or stopping is set.
    std::condition_variable ready;

    /// Submitted utterances not yet handled.
    std::deque<Samples> pending;

    /// Set by the destructor; the thread finishes the queue and returns.
    bool stopping = false;

    /// Thread running run(); declared last so it starts after every other member exists.
    std::thread thread;

    /**
     * @brief Handle utterances until stopping and the queue is empty; runs on the worker thread.
     */
    void run();

    /**
     * @brief Apply an edited phrase file, reporting each distinct rejection once.
     */
    void reloadConfiguration();

    /**
     * @brief Transcribe one utterance, check the wake word, then parse and execute its command.
     */
    void handle(Samples utterance);

public:
    /**
     * @brief Start the worker thread.
     * @throws std::system_error If the thread cannot start.
     */
    CommandWorker(Speech::Transcriber& transcriber, Commands::Parser& parser, Commands::Executor& executor,
                  Config::Configuration& configuration, Speech::SpeechService& speech, string wakeWord);

    /**
     * @brief Finish every submitted utterance, then stop the thread.
     */
    ~CommandWorker();

    /**
     * @brief Copying is disabled; each instance owns its thread.
     */
    CommandWorker(const CommandWorker&) = delete;

    /**
     * @brief Assignment is disabled; each instance owns its thread.
     */
    CommandWorker& operator=(const CommandWorker&) = delete;

    /**
     * @brief Queue an utterance for handling without waiting for it.
     */
    void submit(Samples utterance);
};