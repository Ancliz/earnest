/**
 * @file
 * @brief Utterance queue and the worker thread that handles it.
 */

#include "command_worker.hpp"
#include "spoken_reply.hpp"
#include "parser/speech_text.hpp"

#include <algorithm>
#include <chrono>
#include <spdlog/spdlog.h>
#include <vector>

using Commands::json;
using std::lock_guard;
using std::mutex;
using std::string;

void logParsed(const Commands::ParseResult& parsed) {
    if(parsed) {
        spdlog::info("Command recognized: {}{}", json(*parsed).dump(), parsed->reply.empty() ? "" : " (reply: " + parsed->reply + ")");
    } else if(!parsed.answer.empty()) {
        spdlog::info("Answering: {}", parsed.answer);
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

void learnDeviceNames(Speech::Transcriber& transcriber, const Commands::Devices& devices) {
    // No devices means the servers are down, which shouldn't erase what was learned.
    if(devices.empty()) {
        return;
    }

    std::vector<string> names;

    for(const auto& device : devices) {
        names.push_back(device.name);
    }

    transcriber.setVocabulary(names);
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
                             Config::Configuration& configuration, Speech::SpeechService& speech, string wakeWord,
                             ExchangeObserver observer, std::chrono::milliseconds followUp)
    : transcriber(transcriber), speech(speech), parser(parser), executor(executor), configuration(configuration),
      wakeWord(std::move(wakeWord)), followUp(followUp), observer(std::move(observer)), thread(&CommandWorker::run, this) {}

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

        pending.push_back({std::move(utterance), std::chrono::steady_clock::now()});
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

        enter(Stage::Idle);
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

void CommandWorker::enter(Stage next) {
    static const char* const names[] = {"idle", "transcribing", "parsing", "executing", "speaking"};
    spdlog::trace("Worker is {}", names[static_cast<int>(next)]);
    stage = next;
}

bool CommandWorker::followsUp(std::chrono::steady_clock::time_point heardAt) const {
    if(followUp.count() <= 0 || !repliedAt) {
        return false;
    }

    // Speech heard before the conversation began, even if handled after, wasn't part of it.
    const auto from = std::max(*repliedAt, speech.finishedAt());
    return heardAt > conversationHeardAt && heardAt <= from + followUp;
}

void CommandWorker::keepListening(std::chrono::steady_clock::time_point heardAt) {
    if(!followsUp(heardAt)) {
        conversationHeardAt = heardAt;
    }

    repliedAt = std::chrono::steady_clock::now();
}

void CommandWorker::handle(Queued utterance) {
    enter(Stage::Transcribing);
    const string heard = transcriber.transcribe(std::move(utterance.samples));

    if(heard.find_first_not_of(" \t\r\n") == string::npos) {
        return;
    } else if(Grammar::isNonSpeech(heard)) {
        spdlog::debug("Ignoring non-speech: {}", heard);
        return;
    }

    spdlog::info("Input: {}", heard);
    auto text = Grammar::stripWakeWord(heard, wakeWord);

    if(!text && followsUp(utterance.heardAt)) {
        spdlog::info("Taking this as a follow-up, without the wake word");
        text = Grammar::stripWakeWord(heard, "");
    }

    if(!text) {
        spdlog::debug("Ignoring speech without the wake word: {}", heard);
        return;
    } else if(text->empty()) {
        spdlog::info("Wake word heard with no command");
        keepListening(utterance.heardAt);
        return;
    }

    Exchange exchange;
    exchange.heardAt = utterance.heardAt;
    exchange.transcript = heard;
    exchange.request = *text;
    reloadConfiguration();
    enter(Stage::Parsing);
    auto devices = executor.discoverDevices();

    // Startup may have run before the servers were up, and devices come and go.
    learnDeviceNames(transcriber, devices);
    Commands::Context context(std::move(devices), executor.discoverServices());
    context.defaultDevice = memory.recall(exchange.heardAt);
    context.history = memory.recent(exchange.heardAt);
    bool streamed = false;

    // Sentences are generated here as they arrive, while earlier ones play.
    context.onSentence = [&](const string& sentence) {
        streamed = true;
        enter(Stage::Speaking);
        say(speech, sentence);
    };
    exchange.parsed = parser.parse(*text, context);
    logParsed(exchange.parsed);

    if(exchange.parsed) {
        enter(Stage::Executing);
        exchange.result = executor.execute(*exchange.parsed);
        logResult(*exchange.result, false);
        exchange.spoken = spokenReply(*exchange.parsed, *exchange.result);
    } else if(!exchange.parsed.refusal.empty()) {
        exchange.spoken = spokenFailure(exchange.parsed.refusal);
    } else {
        exchange.spoken = exchange.parsed.answer;
    }

    if(!exchange.spoken.empty() && !streamed) {
        enter(Stage::Speaking);
        say(speech, exchange.spoken);
    }

    memory.remember(exchange);

    // Only a reply or an action keeps the conversation going, so speech the model ignored can't hold it open.
    if(!exchange.spoken.empty() || exchange.result) {
        keepListening(exchange.heardAt);
    }

    if(observer) {
        observer(exchange);
    }
}