/**
 * @file
 * @brief Composition root: selects the transcriber, synthesiser, parser and executor and runs the chosen mode.
 */

#include "settings.hpp"
#include "command_worker.hpp"
#include "commands/executor_router.hpp"
#include "parser/fallback_parser.hpp"
#include "audio/microphone.hpp"
#include "audio/utterance_buffer.hpp"
#include "home_assistant/home_assistant.hpp"
#include "ollama/ollama_parser.hpp"
#include "parser/regex_parser.hpp"
#include "app/logging.hpp"
#include "speech/whisper_transcriber.hpp"
#include "spotify/spotify.hpp"
#include "speech/chatterbox_synthesiser.hpp"
#include "audio/portaudio_player.hpp"

#include <csignal>
#include <spdlog/spdlog.h>
#include <memory>
#include <utility>
#include <vector>

using Commands::json;
using Config::Configuration;
using Speech::Transcriber;
using std::string;
using std::unique_ptr;
using std::make_unique;

namespace {

/// Cleared by SIGINT or SIGTERM to stop listening.
volatile std::sig_atomic_t running = 1;

/**
 * @brief Request a graceful stop; listening flushes buffered speech before returning.
 */
void requestStop(int) {
    running = 0;
}

/**
 * @brief Print each device as "entity | name | state", followed by its room when it has one.
 */
void printDevices(const Devices& devices) {
    for(const auto& device : devices) {
        spdlog::info("{} | {} | {}{}", device.entityId, device.name, device.state, device.room.empty() ? "" : " | " + device.room);
    }
}

/**
 * @brief Parse typed text against live devices and print what would be sent, without sending it.
 * @return Whether a command was recognized and accepted.
 */
bool previewText(const string& text, Parser& parser, Executor& executor) {
    spdlog::info("Input: {}", text);
    const auto command = parser.parse(text, {executor.discoverDevices(), executor.discoverServices()});
    logParsed(command);
    return command && logResult(executor.preview(*command), true);
}

/**
 * @brief Listen until SIGINT or SIGTERM, handing each utterance to a CommandWorker.
 *
 * This thread only captures and segments audio, so speech is still heard while a command runs.
 * A stop request still executes speech that is already buffered or queued.
 */
void listen(const Settings& settings, Transcriber& transcriber, Speech::Synthesiser& synthesiser, Parser& parser,
            Executor& executor, Configuration& configuration) {
    std::signal(SIGINT, requestStop);
    std::signal(SIGTERM, requestStop);
    Microphone microphone;
    UtteranceBuffer utterances(settings.threshold, settings.silenceMs);
    Audio::PortAudioPlayer player;
    Speech::SpeechService speech(synthesiser, player);
    CommandWorker worker(transcriber, parser, executor, configuration, speech, settings.wakeWord);
    Samples block;
    spdlog::info("Listening. Press Ctrl+C to stop.");

    while(running) {
        bool discontinuity = false;

        if(!microphone.read(block, discontinuity)) {
            continue;
        }

        // What the microphone hears now is the assistant's own voice, and its echo until the room goes quiet.
        if(worker.isSpeaking()) {
            utterances.abandon();
            continue;
        }

        auto utterance = utterances.append(block, discontinuity);

        if(utterances.event() == UtteranceBuffer::Event::Discontinuity) {
            spdlog::warn("Microphone overflow: discarding incomplete command");
        } else if(utterances.event() == UtteranceBuffer::Event::TooLong) {
            spdlog::warn("Utterance exceeded 15 seconds; waiting for silence");
        }

        if(utterance) {
            worker.submit(std::move(*utterance));
        }
    }

    // Speech still buffered when stopping is a complete command; the worker finishes it on exit.
    if(auto utterance = utterances.finish()) {
        worker.submit(std::move(*utterance));
    }
}
} // namespace

/**
 * @brief Select the pipeline implementations and run the mode chosen on the command line.
 * @return Zero on success; one for an unrecognized or rejected --parse command or a fatal error.
 */
int main(int argc, char* argv[]) {
    try {
        const auto settings = parseArguments(argc, argv);
        setupLogging(settings.logLevel, settings.logFile);

        if(settings.mode == Mode::Help) {
            printUsage(argv[0]);
            return 0;
        }

        const auto connection = readConnectionSettings();
        const auto kind = settings.parser ? *settings.parser : parserKind(connection.parser);
        RegexParser rules;
        OllamaParser model(connection.ollamaUrl, settings.ollamaModel.empty() ? connection.ollamaModel : settings.ollamaModel);
        FallbackParser hybrid(rules, model);
        Parser& parser = kind == ParserKind::Regex ? static_cast<Parser&>(rules) : kind == ParserKind::Ollama ? static_cast<Parser&>(model) : hybrid;

        HomeAssistant homeAssistant(connection.url, connection.token);
        Spotify spotify(connection.spotifyUrl);
        // Spotify owns only its own domain; everything else, playback controls included, is Home Assistant's.
        auto executors = connection.spotifyUrl.empty() ? std::vector<Executor*>{&homeAssistant} : std::vector<Executor*>{&spotify, &homeAssistant};
        unique_ptr<Executor> executor = make_unique<ExecutorRouter>(executors);
        Configuration configuration(settings.phraseFile, [&](const json& candidate) {
            executor->configure(candidate);
            parser.configure(candidate);
        }, settings.homeFile);

        if(settings.mode == Mode::CheckPhrases) {
            spdlog::info("Phrase configuration is valid");
            return 0;
        }

        if(connection.token.empty()) {
            throw std::runtime_error("Set HOME_ASSISTANT_TOKEN before contacting Home Assistant");
        }

        if(settings.mode == Mode::ListDevices) {
            printDevices(executor->discoverDevices());
            return 0;
        }

        if(settings.mode == Mode::Parse) {
            return previewText(settings.parseText, parser, *executor) ? 0 : 1;
        }

        unique_ptr<Transcriber> transcriber = make_unique<WhisperTranscriber>(settings.model, settings.threads, settings.gpu);
        learnDeviceNames(*transcriber, executor->discoverDevices());

        Speech::ChatterboxSynthesiser synthesiser(connection.chatterboxUrl, connection.chatterboxVoice);
        listen(settings, *transcriber, synthesiser, parser, *executor, configuration);
        
    } catch(const std::exception& error) {
        spdlog::error(error.what());
        return 1;
    }

    return 0;
}