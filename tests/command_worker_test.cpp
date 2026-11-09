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
    std::optional<Command> parse(const string& text, const Context&) override {
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

    /**
     * @brief Accept any configuration.
     */
    void configure(const json&) override {}

    /**
     * @brief No devices; the echo parser needs none.
     */
    Commands::Devices discoverDevices() override { return {}; }

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