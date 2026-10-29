/**
 * @file
 * @brief Composition root: selects the transcriber, parser and executor and runs the chosen mode.
 */

#include "settings.hpp"
#include "audio/microphone.hpp"
#include "audio/utterance_buffer.hpp"
#include "home_assistant/home_assistant.hpp"
#include "ollama/ollama_parser.hpp"
#include "app/logging.hpp"
#include "speech/whisper_transcriber.hpp"

#include <chrono>
#include <csignal>
#include <spdlog/spdlog.h>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

using Commands::Command;
using Commands::json;
using std::optional;
using std::string;

/// Monotonic clock for the reload timer.
using Clock = std::chrono::steady_clock;

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
 * @brief Print recognized text and the command parsed from it.
 */
void printParsed(const string& text, const optional<Command>& command) {
    spdlog::info("Input: {}", text);

    if(command) {
        spdlog::info("Command recognized: {}", json(*command).dump());
    } else {
        spdlog::info("No command recognized");
    }
}

/**
 * @brief Print a preview or execution outcome.
 * @param[in] preview Whether the result came from preview() rather than execute().
 * @return Whether the command was accepted.
 */
bool printResult(const Commands::Result& result, bool preview) {
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
    printParsed(text, command);
    return command && printResult(executor.preview(*command), true);
}

/**
 * @brief Listen until SIGINT or SIGTERM and execute the command in each utterance.
 *
 * Reloads the phrase file every second and before each command. A stop request still
 * executes speech that is already buffered.
 */
void listen(const Settings& settings, Speech::Transcriber& transcriber, Commands::Parser& parser,
            Commands::Executor& executor, Config::Configuration& configuration) {
    std::signal(SIGINT, requestStop);
    std::signal(SIGTERM, requestStop);
    Microphone microphone;
    UtteranceBuffer utterances(settings.threshold, settings.silenceMs);
    string lastReloadError;

    const auto reloadConfiguration = [&] {
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
    };
    
    /*
     * @brief Transcribe one utterance, then parse it against live devices and execute the result.
     */
    const auto executeUtterance = [&](Audio::Samples utterance) {
        const string text = transcriber.transcribe(std::move(utterance));

        if(text.find_first_not_of(" \t\r\n") == string::npos) {
            return;
        }

        reloadConfiguration();

        try {
            const auto command = parser.parse(text, {executor.discoverDevices()});
            printParsed(text, command);

            if(command) {
                printResult(executor.execute(*command), false);
            }
        } catch(const std::exception& error) {
            spdlog::error("Cannot prepare command: {}", error.what());
        }
    };

    spdlog::info("Listening. Press Ctrl+C to stop.");
    auto lastReload = Clock::now();
    Audio::Samples block;

    while(running) {
        if(Clock::now() - lastReload >= std::chrono::seconds(1)) {
            reloadConfiguration();
            lastReload = Clock::now();
        }

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
            executeUtterance(std::move(*utterance));
            // Audio queued while the command ran is stale, and a slow command overflows the queue.
            microphone.discardPending();
        }
    }

    // Speech still buffered when stopping is a complete command.
    if(auto utterance = utterances.finish()) {
        executeUtterance(std::move(*utterance));
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