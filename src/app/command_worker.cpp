/**
 * @file
 * @brief Utterance queue and the worker thread that handles it.
 */

#include "command_worker.hpp"
#include "spoken_reply.hpp"
#include "parser/speech_text.hpp"

#include <chrono>
#include <spdlog/spdlog.h>

using Commands::json;
using std::lock_guard;
using std::mutex;
using std::string;

void logParsed(const std::optional<Commands::Command>& command) {
    if(command) {
        spdlog::info("Command recognized: {}{}", json(*command).dump(), command->reply.empty() ? "" : " (reply: " + command->reply + ")");
    } else {
        spdlog::info("No command recognized");
    }
}

bool logResult(const Commands::Result& result, bool preview) {
    if(!result.success) {
        spdlog::warn("{}{}", preview ? "Would reject: " : "Command failed: ", result.error);
    } else if(preview) {
        spdlog::info("Would send: {}", json(result.command).dump());
    } else {
        spdlog::info("Command sent");
    }

    return result.success;
}

/**
 * @brief Say the text; by now the command has run or been refused, so a speech failure is only logged.
 */
void say(Speech::SpeechService& speech, const string& text) {
    try {
        speech.speak(text);
    } catch(const std::exception& error) {
        spdlog::warn("Cannot speak: {}", error.what());
    }
}

CommandWorker::CommandWorker(Speech::Transcriber& transcriber, Commands::Parser& parser, Commands::Executor& executor,
                             Config::Configuration& configuration, Speech::SpeechService& speech, string wakeWord)
    : transcriber(transcriber), speech(speech), parser(parser), executor(executor), configuration(configuration),
      wakeWord(std::move(wakeWord)), thread(&CommandWorker::run, this) {}

CommandWorker::~CommandWorker() {
    {
        lock_guard<mutex> lock(queueMutex);
        stopping = true;
    }

    ready.notify_one();
    thread.join();
}

void CommandWorker::submit(Samples utterance) {
    {
        lock_guard<mutex> lock(queueMutex);

        if(pending.size() >= maxPending) {
            spdlog::warn("Commands are backing up: dropping the oldest unhandled utterance");
            pending.pop_front();
        }

        pending.push_back(std::move(utterance));
    }

    ready.notify_one();
}

void CommandWorker::run() {
    while(true) {
        std::unique_lock<mutex> lock(queueMutex);

        // Wake for new speech or a stop request, or after 5 seconds idle to check the configuration files.
        if(!ready.wait_for(lock, std::chrono::seconds(5), [&] { return stopping || !pending.empty(); })) {
            lock.unlock();
            reloadConfiguration();
            continue;
        }

        if(pending.empty()) {
            return;
        }

        auto utterance = std::move(pending.front());
        pending.pop_front();
        lock.unlock();

        // One failed utterance must not end the thread and silence every later command.
        try {
            handle(std::move(utterance));
        } catch(const std::exception& error) {
            spdlog::error("Cannot handle utterance: {}", error.what());
        }
    }
}

void CommandWorker::reloadConfiguration() {
    try {
        if(configuration.reloadIfChanged()) {
            spdlog::info("Reloaded phrase configuration");
        }

        lastReloadError.clear();
    } catch(const std::exception& error) {
        if(lastReloadError != error.what()) {
            spdlog::warn("Keeping previous configuration: {}", error.what());
        }

        lastReloadError = error.what();
    }
}

void CommandWorker::handle(Samples utterance) {
    const string heard = transcriber.transcribe(std::move(utterance));

    if(heard.find_first_not_of(" \t\r\n") == string::npos) {
        return;
    } else if(Grammar::isNonSpeech(heard)) {
        spdlog::debug("Ignoring non-speech: {}", heard);
        return;
    }

    spdlog::info("Input: {}", heard);
    const auto text = Grammar::stripWakeWord(heard, wakeWord);

    if(!text) {
        spdlog::debug("Ignoring speech without the wake word: {}", heard);
        return;
    } else if(text->empty()) {
        spdlog::info("Wake word heard with no command");
        return;
    }

    reloadConfiguration();
    const auto command = parser.parse(*text, {executor.discoverDevices(), executor.discoverServices()});
    logParsed(command);

    if(command) {
        const auto result = executor.execute(*command);
        logResult(result, false);
        say(speech, spokenReply(*command, result));
    } else if(const auto reason = parser.failureReason(); !reason.empty()) {
        say(speech, spokenFailure(reason));
    }
}