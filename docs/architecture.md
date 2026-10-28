# Architecture and side-effect audit

Reviewed on 2026-10-27: all project-owned C++ sources and headers, CMake targets,
configuration integration, and regression tests. Generated build trees and vendored
Whisper sources were outside this application-code review.

## Findings and changes

| Finding | Change |
| --- | --- |
| `processTranscript` fetched devices, reloaded files, parsed text, logged, and posted commands. Its name concealed device-control side effects. | Removed it. The application visibly sequences discovery, parsing, reporting, and `executor.execute`. |
| `processUtterance` was a redundant wrapper that mixed speech recognition with control. Whisper was buried in `main.cpp`. | Removed the wrapper. `WhisperTranscriber` owns Whisper and only returns text. Normal utterances and shutdown flushing share one processing block. |
| The application depended on the concrete regex engine. | Added `Commands::Parser` with virtual `configure`/`parse`. `RegexParser` implements it; the composition root selects the backend once. Context contains values, not a Home Assistant client. |
| A global convenience parser retained shared rules and silently read/reloaded files during parsing. | Removed `Parser::parse` and the file-owning `Engine`. Parser instances have independent snapshots and explicit configuration. |
| Grammar compilation also validated installation control policy. | Application configuration validates policy separately before configuring the parser. The executor independently validates policy as well. |
| Configuration reload lived inside the regex implementation, forcing other backends to depend on it for policy. | `Config::Configuration` owns YAML/file access and the active snapshot. Its callback prepares the selected backend; invalid candidates preserve the active parser and policy. |
| Dry runs skipped service availability checks that real execution performed. | Added `Commands::Executor::check` and `execute`; execution calls the same check before its single POST. Both return structured results. |
| `/conversation/process` unconditionally bypassed command validation because the executor trusted the regex parser. | Reject conversation execution. Relative volume is now a structured adjustment resolved from fresh volume state and converted to a direct `volume_set` request. |
| Service validation used one service-name list for every domain and omitted some required fields. | Share a domain-specific service/field catalog between grammar compilation and execution. Require percentage, position, and other mandatory parameters independently of parsing. |
| JSON type errors could escape pure command validation. Temperature had an unrelated generic 0–100 bound. | Return explicit rejection results for malformed commands/state/policy; use finite device-supplied temperature limits and step. |
| Default media-player targeting could accept a configured climate entity. | Restrict default targets to the requested domain; unresolved media names remain available only outside strict discovery. |
| Audio buffering depended on CLI settings and wrote diagnostics directly to stderr. | Constructor accepts audio parameters and validates them. Buffering reports discard events; the application chooses how to display them. |
| Microphone destruction joined its capture thread before interrupting a potentially blocked audio read. | Abort the stream before joining, then close it. Hardware-specific shutdown behavior still needs a live microphone check. |
| `.env` loading changed process-wide environment variables, including unrelated keys. | Return typed Home Assistant connection settings without `setenv`; preserve environment-over-file precedence. |
| Conflicting CLI modes silently overrode or shadowed one another. | Reject combinations of discovery, configuration checking, and parse-only mode. |
| YAML alias expansion had a depth bound but no total-work bound; file reads could grow beyond the initial size check. | Bound expanded nodes, cap reads to the checked size, and reject files that change length during a read. |
| HTTP client tests were coupled to the Whisper/PortAudio build. | HTTP client/tests build with voice disabled. Separate configuration, shared context, regex backend, executor, audio, and settings targets. |

## Side-effect ownership

| Component | Permitted effects |
| --- | --- |
| `RegexParser::parse`, name resolution, numeric normalization | None beyond returned values/local allocations. No HTTP, disk reads, output, reloads, or command execution. |
| `RegexParser::configure` / `addPhrase` | Explicitly replace or extend that instance's compiled grammar. Failed configuration preserves its prior state. |
| `Config::readYaml` | Decode supplied text only. |
| `Config::Configuration` constructor / reload methods | Read the specified file, prepare consumers through the supplied callback, and commit valid configuration. No logging or device calls. |
| `buildCommandContext` / `findAvailableDevices` | Return values derived from supplied state. Caller-owned options are not mutated. |
| `prepareCommand` / `validateCommand` | Resolve and validate against supplied state/policy. No HTTP or output. |
| `HomeAssistant::getStates`, `getServices`, `check` | Read-only HTTP. Failures throw for discovery or return errors for checks. |
| `HomeAssistant::execute` | Fresh read-only checks followed by at most one control POST. No retries or application logging. |
| `WhisperTranscriber` | Model loading and inference; pads its owned audio copy. Never parses or executes commands. Whisper itself may emit library diagnostics. |
| `Microphone` | Own PortAudio initialization, capture thread, bounded queue, and cleanup. |
| `UtteranceBuffer` | Mutate its owned audio buffers and return completion/discard information. No output or CLI dependency. |
| `readHomeAssistantSettings` | Read process environment and optional settings file; return settings without modifying either. |
| Application / CLI help | Select implementations, reload explicitly, wire stages, and display diagnostics/results. |

## Selecting another parser

An LLM backend implements `Commands::Parser::configure` and `parse`. Select that
class instead of `Parser::RegexParser` in the composition root. It may call a model
server to obtain a command, but must not execute device commands. `configure` must
prepare candidate state before replacing the active state. Configuration callbacks
must have no other external side effects, because those effects cannot be rolled
back when a candidate fails.

The audio and executor layers neither know nor inspect the parser's concrete type.
An alternate implementation is exercised through the interface in the boundary
regression suite. An actual Ollama adapter is not part of this refactor.

## Remaining limits

- The command contract currently represents one request or no command. Multiple
  tool calls, conversational replies, research, and desktop executors need their
  own deliberate API extension; they are not implemented by this refactor.
- Commands retain the existing Home Assistant JSON representation. Relative volume
  now uses an internal endpoint; it is resolved locally and never POSTed as-is.
- Live-state checking and posting are separate HTTP operations. Another client
  may change state between them; the application cannot make Home Assistant
  service execution atomic. Relative volume has the same read/modify/write limit.
- Capture is threaded, but transcription, discovery, parsing, and execution are
  sequential. Slow inference/network calls can overflow the bounded capture queue;
  incomplete audio is discarded. This is not yet a concurrent assistant runtime.
- RMS detection, a five-second queue, and a 15-second utterance limit remain. The
  microphone's format support, actual Whisper inference, and shutdown behavior
  require hardware tests; mock HTTP/unit checks do not establish those properties.
- Configuration and parser mutation are single-threaded. Share them across threads
  only with explicit synchronization. Local YAML regexes remain trusted inputs;
  size/node bounds do not impose a time limit on `std::regex` matching.
- Constructor effects are intentional resource acquisition: configuration reads a
  file, Whisper loads a model, and the microphone opens/starts capture. None of
  these constructors sends a control command. Home Assistant construction configures
  its client without performing discovery or posting.
