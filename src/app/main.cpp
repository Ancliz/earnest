/**
 * @file
 * @brief Composition root: selects the transcriber, parser and executor and runs the chosen mode.
 */

#include "settings.hpp"
#include "command_worker.hpp"
#include "audio/microphone.hpp"
#include "audio/utterance_buffer.hpp"
#include "home_assistant/home_assistant.hpp"
#include "ollama/ollama_parser.hpp"
#include "app/logging.hpp"
#include "speech/whisper_transcriber.hpp"

#include <csignal>
#include <spdlog/spdlog.h>
#include <memory>
#include <utility>
#include <vector>

using Commands::json;
using std::string;

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
 * @brief Print each device as "entity | name | state".
 */
void printDevices(const Commands::Devices& devices) {
    for(const auto& device : devices) {
        spdlog::info("{} | {} | {}", device.entityId, device.name, device.state);
    }
}

/**
 * @brief Parse typed text against live devices and print what would be sent, without sending it.
 * @return Whether a command was recognized and accepted.
 */
bool previewText(const string& text, Commands::Parser& parser, Commands::Executor& executor) {
    const auto command = parser.parse(text, {executor.discoverDevices()});
    logParsed(text, command);
    return command && logResult(executor.preview(*command), true);
}

/**
 * @brief Listen until SIGINT or SIGTERM, handing each utterance to a CommandWorker.
 *
 * This thread only captures and segments audio, so speech is still heard while a command runs.
 * A stop request still executes speech that is already buffered or queued.
 */
void listen(const Settings& settings, Speech::Transcriber& transcriber, Commands::Parser& parser,
            Commands::Executor& executor, Config::Configuration& configuration) {
    std::signal(SIGINT, requestStop);
    std::signal(SIGTERM, requestStop);
    Microphone microphone;
    UtteranceBuffer utterances(settings.threshold, settings.silenceMs);
    CommandWorker worker(transcriber, parser, executor, configuration, settings.wakeWord);
    Audio::Samples block;
    spdlog::info("Listening. Press Ctrl+C to stop.");

    while(running) {
        bool discontinuity = false;

        if(!microphone.read(block, discontinuity)) {
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

        const auto connection = readHomeAssistantSettings();
        // Swap point: each stage only sees the others through the Commands and Speech interfaces.
        std::unique_ptr<Commands::Parser> parser = std::make_unique<OllamaParser>();
        std::unique_ptr<Commands::Executor> executor =
            std::make_unique<HomeAssistant>(connection.url, connection.token);
        Config::Configuration configuration(settings.phraseFile, [&](const json& candidate) {
            executor->configure(candidate);
            parser->configure(candidate);
        });

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
            return previewText(settings.parseText, *parser, *executor) ? 0 : 1;
        }

        auto whisper = std::make_unique<WhisperTranscriber>(settings.model, settings.threads, settings.gpu);
        std::vector<string> deviceNames;

        // Device names spelled as Home Assistant spells them help Whisper hear them.
        for(const auto& device : executor->discoverDevices()) {
            deviceNames.push_back(device.name);
        }

        whisper->setVocabulary(deviceNames);
        std::unique_ptr<Speech::Transcriber> transcriber = std::move(whisper);

        listen(settings, *transcriber, *parser, *executor, configuration);
        
    } catch(const std::exception& error) {
        spdlog::error(error.what());
        return 1;
    }

    return 0;
}