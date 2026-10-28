/**
 * @file
 * @brief CLI composition root for explicit speech, parsing, checking, and execution stages.
 */

#include "settings.hpp"
#include "audio/microphone.hpp"
#include "audio/utterance_buffer.hpp"
#include "home_assistant/control_policy.hpp"
#include "home_assistant/device_aliases.hpp"
#include "home_assistant/home_assistant.hpp"
#include "parser/parser.hpp"
#include "speech/transcriber.hpp"

#include <chrono>
#include <csignal>
#include <iostream>
#include <memory>
#include <utility>

using Commands::json;
using std::string;

namespace {
volatile std::sig_atomic_t running = 1;

/**
 * @brief Request graceful application shutdown from a process signal handler.
 *
 * The unnamed argument is the delivered signal number and is intentionally ignored.
 * Only the signal-safe flag assignment occurs here; the main loop later flushes
 * buffered speech and allows resource-owning objects to clean up.
 *
 * @note Does not perform I/O, lock mutexes, allocate memory, or execute a command.
 */
void handleSignal(int) {
    running = 0;
}

/**
 * @brief Print a completed transcript and its proposed command for debugging.
 *
 * @param[in] text Recognized or typed text to display after the Final label.
 * @param[in] request Parsed command JSON, or null when no command was recognized.
 * @note Writes standard output only; does not parse, validate, or execute the request.
 */
void debugPrintTranscript(const string& text, const json& request) {
    std::cout << "Final: " << text << '\n';

    if(request.is_null()) {
        std::cout << "No command recognized\n";
    } else {
        std::cout << "Command recognized: " << request << '\n';
    }
}

/**
 * @brief Display a preview/execution outcome and preserve its success status.
 *
 * @param[in] result Structured executor result, including error or resolved request.
 * @param[in] preview Whether successful output describes a hypothetical submission.
 * @return result.success, suitable for CLI exit-code selection by the caller.
 * @note Writes failures to stderr and successful outcomes to stdout. This reporting
 *       flag cannot enable execution and does not itself send a request.
 */
bool reportResult(const Commands::ExecutionResult& result, bool preview) {
    if(!result.success) {
        std::cerr << (preview ? "Would reject: " : "Command failed: ") << result.error << '\n';
    } else if(preview) {
        std::cout << "Would send: " << result.request << '\n';
    } else {
        std::cout << "Command sent\n";
    }

    return result.success;
}
} // namespace

/**
 * @brief Compose recognition, parsing, configuration, and execution for the CLI application.
 *
 * Selects a parser implementation, validates the initial configuration, and handles
 * help, configuration checking, discovery, or read-only parsing before opening audio.
 * Listening mode captures completed utterances, transcribes them, explicitly parses
 * with fresh discovery context, and explicitly executes accepted commands. Polls
 * configuration and handles normal/shutdown completion through one processing block.
 *
 * @param[in] argc Process argument count, including the executable name.
 * @param[in] argv CLI arguments passed to parseArguments().
 * @return Zero for successful completion, one for failed parsing/preview or a caught
 *         fatal application exception.
 * @note This is the composition root: it owns output, file reloads, signal handlers,
 *       audio/model resource acquisition, and the decision to execute device control.
 */
int main(int argc, char* argv[]) {
    if(argc == 2 && (string(argv[1]) == "--help" || string(argv[1]) == "-h")) {
        printUsage(argv[0]);
        return 0;
    }

    try {
        const auto settings = parseArguments(argc, argv);
        // The composition root selects the backend. No audio or executor code depends on RegexParser.
        std::unique_ptr<Commands::Parser> parser = std::make_unique<Parser::RegexParser>();
        /*
         * @brief Validate policy before preparing the selected parser for a configuration candidate.
         *
         * @param[in] candidate Decoded replacement snapshot supplied by Config::Configuration.
         * @throws std::exception If policy validation or parser preparation fails.
         * @note Mutates only parser-owned state after policy validation. The configuration
         *       controller commits its active snapshot only after this callback returns.
         */
        Config::Configuration configuration(settings.phrases, [&](const json& candidate) {
            validateControlConfiguration(candidate);
            parser->configure(candidate);
        });

        if(settings.checkPhrases) {
            std::cout << "Phrase configuration is valid\n";
            return 0;
        }

        const auto connection = readHomeAssistantSettings();
        HomeAssistant assistant(connection.url, connection.token);
        Commands::Executor& executor = assistant;

        if(settings.listDevices) {
            for(const auto& device : findAvailableDevices(assistant.getStates())) {
                std::cout << device.entityId << " | " << device.friendlyName << " | " << device.state << '\n';
            }

            return 0;
        }

        if(settings.parseOnly) {
            const auto context = buildCommandContext(assistant.getStates());
            const auto command = parser->parse(settings.parseText, context);
            debugPrintTranscript(settings.parseText, command);
            if(command.is_null()) {
                return 1;
            }

            return reportResult(executor.check(command, configuration.data()), true) ? 0 : 1;
        }

        WhisperTranscriber transcriber(settings.model, settings.threads, settings.gpu);
        std::signal(SIGINT, handleSignal);
        std::signal(SIGTERM, handleSignal);
        Microphone microphone;
        UtteranceBuffer utterances(settings.threshold, settings.silenceMs);
        Audio::Samples block;
        std::cout << "Listening. Press Ctrl+C to stop.\n";
        auto lastReload = std::chrono::steady_clock::now();
        string lastReloadError;
        /*
         * @brief Poll configuration explicitly and report changes or newly changed errors.
         *
         * Retains the previous valid snapshot on failure and remembers the last error
         * printed so an unchanged failure is not emitted for every audio block.
         *
         * @note Reads the configuration file, may reconfigure the parser, writes diagnostic
         *       output, and updates the captured last-error string.
         */
        const auto reloadConfiguration = [&] {
            if(configuration.reloadIfChanged()) {
                std::cout << "Reloaded phrase configuration\n";
            }

            if(configuration.error() != lastReloadError && !configuration.error().empty()) {
                std::cerr << "Keeping previous configuration: " << configuration.error() << '\n';
            }

            lastReloadError = configuration.error();
        };

        while(true) {
            const bool stopping = !running;
            const auto now = std::chrono::steady_clock::now();

            if(now - lastReload >= std::chrono::seconds(1)) {
                reloadConfiguration();
                lastReload = now;
            }

            std::optional<Audio::Samples> completed;

            if(stopping) {
                completed = utterances.finish();
            } else {
                bool discontinuity = false;

                if(!microphone.read(block, discontinuity)) {
                    continue;
                }

                completed = utterances.append(block, discontinuity);

                if(utterances.event() == UtteranceBuffer::Event::Discontinuity) {
                    std::cerr << "Microphone overflow: discarding incomplete command\n";
                } else if(utterances.event() == UtteranceBuffer::Event::TooLong) {
                    std::cerr << "Utterance exceeded 15 seconds; waiting for silence\n";
                }
            }

            if(completed) {
                const string text = transcriber.transcribe(std::move(*completed));

                if(text.find_first_not_of(" \t\r\n") != string::npos) {
                    reloadConfiguration();

                    try {
                        const auto context = buildCommandContext(assistant.getStates());
                        const auto command = parser->parse(text, context);
                        debugPrintTranscript(text, command);

                        if(!command.is_null()) {
                            reportResult(executor.execute(command, configuration.data()), false);
                        }
                    } catch(const std::exception& error) {
                        std::cerr << "Cannot prepare command: " << error.what() << '\n';
                    }
                }
            }

            if(stopping) {
                break;
            }
        }
    } catch(const std::exception& error) {
        std::cerr << "Error: " << error.what() << '\n';
        return 1;
    }

    return 0;
}
