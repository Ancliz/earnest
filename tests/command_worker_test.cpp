/**
 * @file
 * @brief Regression suite for the background command worker.
 */

#include "check.hpp"
#include "app/command_worker.hpp"
#include "app/spoken_reply.hpp"

#include <atomic>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>

using Audio::Samples;
using Commands::Command;
using Commands::Context;
using Commands::json;
using Commands::Result;
using std::chrono::milliseconds;
using std::string;
using std::vector;

namespace {
/**
 * @brief Transcribes an utterance holding {i} as texts[i], optionally holding the first one until released.
 */
class ScriptedTranscriber final : public Speech::Transcriber {
public:
    /// Text returned for each utterance index; "throw" makes transcription fail.
    vector<string> texts;

    /// Whether the first utterance waits for release.
    bool holdFirst = false;

    /// Set once transcription of any utterance has started.
    std::atomic<bool> started{false};

    /// Set by the test to let a held first utterance finish.
    std::atomic<bool> released{false};

    /// Thread that ran the last transcription.
    std::thread::id thread;

    /// Words most recently given to setVocabulary().
    vector<string> vocabulary;

    /**
     * @brief Record the words.
     */
    void setVocabulary(const vector<string>& words) override { vocabulary = words; }

    /**
     * @brief Return the scripted text for the utterance's index.
     */
    string transcribe(Samples utterance) override {
        const auto index = static_cast<size_t>(utterance.at(0));
        thread = std::this_thread::get_id();
        started = true;

        while(holdFirst && index == 0 && !released) {
            std::this_thread::sleep_for(milliseconds(5));
        }

        if(texts.at(index) == "throw") {
            throw std::runtime_error("scripted transcription failure");
        }

        return texts.at(index);
    }
};

/**
 * @brief Parses any text into turning on light.<text>.
 */
class EchoParser final : public Commands::Parser {
public:
    /**
     * @brief Accept any configuration.
     */
    void configure(const json&) override {}

    /**
     * @brief Target the light named by the whole text.
     */
    Commands::ParseResult parse(const string& text, const Context&) override {
        return Command{"light", "turn_on", "light." + text, json::object()};
    }
};

/**
 * @brief Records executed commands instead of sending them.
 */
class RecordingExecutor final : public Commands::Executor {
public:
    /// Entities executed, in order.
    vector<string> executed;

    /// Devices reported by discovery.
    Commands::Devices devices;

    /**
     * @brief Accept any configuration.
     */
    void configure(const json&) override {}

    /**
     * @brief The configured devices; the echo parser needs none.
     */
    Commands::Devices discoverDevices() override { return devices; }

    /**
     * @brief No services; the echo parser needs none.
     */
    Commands::ServiceCatalogue discoverServices() override { return {}; }

    /**
     * @brief Accept every command.
     */
    Result preview(const Command& command) override { return {true, "", command}; }

    /**
     * @brief Record the command's entity.
     */
    Result execute(const Command& command) override {
        executed.push_back(command.entity);
        return {true, "", command};
    }
};

/**
 * @brief An utterance the scripted transcriber reads as texts[index].
 */
Samples utterance(size_t index) { return Samples{static_cast<float>(index)}; }

/**
 * @brief Produces no audio, so the worker needs no speech server.
 */
class SilentSynthesiser final : public Speech::Synthesiser {
public:
    Audio::Buffer generate(std::string_view) override { return {{}, 16000, 1}; }
};

/**
 * @brief Plays nothing, so the worker needs no sound device.
 */
class SilentPlayer final : public Audio::Player {
public:
    void play(const Audio::Buffer&) override {}
};

/**
 * @brief Never finds a command, and gives the configured reason.
 */
class RefusingParser final : public Commands::Parser {
public:
    string reason;

    void configure(const json&) override {}
    Commands::ParseResult parse(const string&, const Context&) override { return Commands::ParseResult::refused(reason); }
};

/**
 * @brief Records what it was asked to say.
 */
class RecordingSynthesiser final : public Speech::Synthesiser {
public:
    vector<string> said;

    Audio::Buffer generate(std::string_view text) override {
        said.emplace_back(text);
        return {{}, 16000, 1};
    }
};

/**
 * @brief Commands light.<text>, noting which device it was told each request could mean by "it".
 */
class FollowUpParser final : public Commands::Parser {
public:
    vector<string> remembered;
    vector<size_t> historySizes;

    void configure(const json&) override {}

    Commands::ParseResult parse(const string& text, const Context& context) override {
        remembered.push_back(context.defaultDevice);
        historySizes.push_back(context.history.size());
        return text == "nothing" ? Commands::ParseResult() : Commands::ParseResult(Command{"light", "turn_on", text == "deviceless" ? "" : "light." + text, json::object()});
    }
};

/**
 * @brief A parser that has heard these requests in turn, with what it was told at each.
 */
FollowUpParser afterRequests(const vector<string>& heard) {
    ScriptedTranscriber transcriber;
    transcriber.texts = heard;
    FollowUpParser parser;
    RecordingExecutor executor;
    Config::Configuration configuration(Config::defaultPhraseFile, [](const json&) {});
    SilentSynthesiser synthesiser;
    SilentPlayer player;
    Speech::SpeechService speech(synthesiser, player);

    {
        std::atomic<size_t> finished{0};
        CommandWorker worker(transcriber, parser, executor, configuration, speech, "", [&](const Exchange&) { ++finished; });

        // One at a time, as the worker only keeps a few waiting.
        for(size_t index = 0; index < heard.size(); ++index) {
            worker.submit(utterance(index));

            while(finished <= index) {
                std::this_thread::sleep_for(milliseconds(1));
            }
        }
    }

    return parser;
}

/**
 * @brief The vocabulary after one command, starting from "old", when discovery finds the given devices.
 */
vector<string> vocabularyAfterCommand(Commands::Devices devices) {
    ScriptedTranscriber transcriber;
    transcriber.texts = {"a"};
    transcriber.vocabulary = {"old"};
    EchoParser parser;
    RecordingExecutor executor;
    executor.devices = std::move(devices);
    Config::Configuration configuration(Config::defaultPhraseFile, [](const json&) {});
    SilentSynthesiser synthesiser;
    SilentPlayer player;
    Speech::SpeechService speech(synthesiser, player);

    {
        CommandWorker worker(transcriber, parser, executor, configuration, speech, "");
        worker.submit(utterance(0));
    }

    return transcriber.vocabulary;
}

/**
 * @brief Notes whether the worker reported speaking while audio played.
 */
class WatchingPlayer final : public Audio::Player {
public:
    /// The worker to ask; set before any audio plays.
    std::atomic<const CommandWorker*> worker{nullptr};

    /// Whether the worker said it was speaking during playback.
    std::atomic<bool> sawSpeaking{false};

    void play(const Audio::Buffer&) override { sawSpeaking = worker.load()->isSpeaking(); }
};

/**
 * @brief Run one command and report its exchange, and whether the worker reported speaking during playback.
 */
std::pair<Exchange, bool> observeExchange(const string& heard) {
    ScriptedTranscriber transcriber;
    transcriber.texts = {heard};
    EchoParser parser;
    RecordingExecutor executor;
    Config::Configuration configuration(Config::defaultPhraseFile, [](const json&) {});
    SilentSynthesiser synthesiser;
    WatchingPlayer player;
    Speech::SpeechService speech(synthesiser, player);
    Exchange seen;
    std::atomic<bool> finished{false};
    bool stoppedSpeaking = false;

    {
        CommandWorker worker(transcriber, parser, executor, configuration, speech, "", [&](const Exchange& exchange) {
            seen = exchange;
            finished = true;
        });
        player.worker = &worker;
        worker.submit(utterance(0));

        while(!finished) {
            std::this_thread::sleep_for(milliseconds(1));
        }

        // The worker leaves the speaking stage just after telling the observer.
        for(int i = 0; i < 2000 && worker.isSpeaking(); ++i) {
            std::this_thread::sleep_for(milliseconds(1));
        }

        stoppedSpeaking = !worker.isSpeaking();
    }

    return {seen, player.sawSpeaking && stoppedSpeaking};
}

/**
 * @brief What the worker says when a parser refuses with the given reason.
 */
vector<string> sayRefusal(const string& reason) {
    ScriptedTranscriber transcriber;
    transcriber.texts = {"a"};
    RefusingParser parser;
    parser.reason = reason;
    RecordingExecutor executor;
    Config::Configuration configuration(Config::defaultPhraseFile, [](const json&) {});
    RecordingSynthesiser synthesiser;
    SilentPlayer player;
    Speech::SpeechService speech(synthesiser, player);

    {
        CommandWorker worker(transcriber, parser, executor, configuration, speech, "");
        worker.submit(utterance(0));
    }

    return synthesiser.said;
}

/**
 * @brief Run texts through a fresh worker and return the entities it executed.
 */
vector<string> runScript(const vector<string>& texts, const string& wakeWord = "") {
    ScriptedTranscriber transcriber;
    transcriber.texts = texts;
    EchoParser parser;
    RecordingExecutor executor;
    Config::Configuration configuration(Config::defaultPhraseFile, [](const json&) {});

    SilentSynthesiser synthesiser;
    SilentPlayer player;
    Speech::SpeechService speech(synthesiser, player);

    {
        CommandWorker worker(transcriber, parser, executor, configuration, speech, wakeWord);

        for(size_t i = 0; i < texts.size(); ++i) {
            worker.submit(utterance(i));
        }
    }

    return executor.executed;
}
} // namespace

/**
 * @brief Verify ordering, draining, non-blocking submission, backlog limits, wake words, and failure recovery.
 */
int main() {
    check(vocabularyAfterCommand({Commands::Device{"light.a", "Desk Lamp", "light", "on"}}) == vector<string>{"Desk Lamp"}, "each command refreshes the transcriber's device names");
    check(vocabularyAfterCommand({}) == vector<string>{"old"}, "an outage does not erase the learned device names");
    const auto [exchange, spokeDuring] = observeExchange("kitchen");
    check(exchange.request == "kitchen" && exchange.parsed && exchange.result && exchange.result->success && !exchange.spoken.empty(), "an exchange records the request, command, outcome and reply");
    check(spokeDuring, "the worker reports speaking only during playback");
    const auto followed = afterRequests({"a", "b", "deviceless", "nothing", "c"});
    check(followed.remembered == vector<string>{"", "light.a", "light.b", "light.b", "light.b"}, "the last device commanded is offered to the next request, and requests without one don't replace it");
    check(followed.historySizes == vector<size_t>{0, 1, 2, 3, 3}, "the worker passes the conversation to the parser, which only holds commands");

    const auto at = [](int seconds) { return std::chrono::steady_clock::time_point(std::chrono::seconds(seconds)); };
    const Command lamp{"light", "turn_on", "light.lamp", json::object()};
    Exchange done;
    done.heardAt = at(1000);
    done.result = Commands::Result{true, "", lamp};
    ShortTermMemory memory;
    check(memory.recall(at(1000)).empty(), "nothing is remembered at first");
    done.result->success = false;
    memory.remember(done);
    check(memory.recall(at(1001)).empty(), "a failed command isn't remembered");
    done.result->success = true;
    memory.remember(done);
    check(memory.recall(at(1299)) == "light.lamp", "a device is remembered for a few minutes");
    check(memory.recall(at(1300)).empty(), "and then forgotten");
    check(memory.recent(at(1299)).size() == 2 && memory.recent(at(1299))[1].succeeded && memory.recent(at(1300)).empty(), "requests expire too");
    done.result->success = false;
    done.request = "failed one";
    memory.remember(done);
    check(memory.recent(at(1001)).size() == 3 && !memory.recent(at(1001))[2].succeeded && memory.recall(at(1001)) == "light.lamp", "a failure is part of the conversation but doesn't become \"it\"");

    for(int request = 0; request < 6; ++request) {
        memory.remember(done);
    }

    check(memory.recent(at(1001)).size() == 4, "only the last few requests are kept");
    check(sayRefusal("q800 isn't a spotify device") == vector<string>{"q800 isn't a spotify device, sir."}, "a refusal says what went wrong");
    check(sayRefusal("I'm sorry sir, I can't.") == vector<string>{"I'm sorry sir, I can't."}, "a reason already addressing sir is said as written");
    check(sayRefusal("I can't find sirius") == vector<string>{"I can't find sirius, sir."}, "only the whole word counts as addressing sir");
    check(sayRefusal("").empty(), "speech the parser has no complaint about stays silent");

    const Command light{"light", "turn_on", "light.a", json::object(), "Turning it on."};
    const Command volume{"media_player", "volume_set", "media_player.a", {{"volume_level", 0.3}}, "Louder."};
    check(spokenReply(light, {true, "", light}) == "Turning it on.", "the parser's wording is said on success");
    check(spokenReply({}, {true, "", {}}) == "Command sent, sir", "success without wording says the default");
    check(spokenReply(light, {false, "broken", light}) == "Command failed, sir.", "a failure is not reported as the parser's wording");
    check(spokenReply(light, {true, "", light, "The answer."}) == "The answer.", "an executor's answer comes first");
    check(spokenReply(volume, {true, "", volume}) == "Volume set to 30 percent.", "a volume change says the level it set");
    check(spokenReply(volume, {false, "broken", volume}) == "Command failed, sir.", "a failed volume change does not claim a level");

    check(runScript({"a", "b", "c"}) == vector<string>{"light.a", "light.b", "light.c"}, "commands run in submission order and drain on exit");
    check(runScript({"a", "throw", "c"}) == vector<string>{"light.a", "light.c"}, "a failed utterance does not stop the worker");
    check(runScript({"  ", "a"}) == vector<string>{"light.a"}, "blank transcripts are skipped");
    check(runScript({"jarvis kitchen", "kitchen", "jarvis"}, "jarvis") == vector<string>{"light.kitchen"}, "the wake word gates commands");

    ScriptedTranscriber transcriber;
    transcriber.texts = {"a", "b", "c", "d", "e"};
    transcriber.holdFirst = true;
    EchoParser parser;
    RecordingExecutor executor;
    Config::Configuration configuration(Config::defaultPhraseFile, [](const json&) {});

    SilentSynthesiser synthesiser;
    SilentPlayer player;
    Speech::SpeechService speech(synthesiser, player);

    {
        CommandWorker worker(transcriber, parser, executor, configuration, speech, "");
        worker.submit(utterance(0));

        while(!transcriber.started) {
            std::this_thread::sleep_for(milliseconds(1));
        }

        const auto before = std::chrono::steady_clock::now();

        for(size_t i = 1; i < 5; ++i) {
            worker.submit(utterance(i));
        }

        check(std::chrono::steady_clock::now() - before < milliseconds(50), "submit does not wait for a running command");
        transcriber.released = true;
    }

    check(transcriber.thread != std::this_thread::get_id(), "commands run on the worker thread");
    check(executor.executed == vector<string>{"light.a", "light.c", "light.d", "light.e"}, "a backlog drops the oldest waiting utterance");
    std::cout << checks << " command worker checks passed\n";
}