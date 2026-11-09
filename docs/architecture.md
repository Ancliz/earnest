# Architecture

```
capture thread:  Microphone → UtteranceBuffer → CommandWorker::submit
worker thread:   Speech::Transcriber → text → wake-word and non-speech filters
text + executor->discoverDevices() + discoverServices() → Commands::Parser → Commands::Command
Commands::Command → Commands::Executor → preview (read-only) or execute (one POST)
Commands::Result → spokenReply → Speech::SpeechService → Speech::Synthesiser → Audio::Player
```

## Threads

- **Microphone capture thread** (`Microphone`): reads PortAudio into a five-second queue.
- **Main thread** (`listen` in `main.cpp`): reads that queue, splits speech into utterances
  and submits them. It never waits on inference or the network, so speech is still heard
  while a command runs.
- **Worker thread** (`CommandWorker`): transcribes, parses and executes each utterance in
  order, and reloads the configuration files every 5 seconds while idle and before each command. It is the only thread that
  touches the transcriber, parser, executor and configuration, so none of them need locks.
  At most three utterances wait; beyond that the oldest is dropped. On shutdown it finishes
  every queued utterance before returning.

Each stage knows the others only through the types in `src/commands/command.hpp` and
`src/speech/`'s `transcriber.hpp` and `synthesiser.hpp`. `src/app/main.cpp` is the only place that names concrete
implementations.

| Interface | Methods | Current implementation |
| --- | --- | --- |
| `Speech::Transcriber` | `transcribe(samples)` | `WhisperTranscriber` |
| `Speech::Synthesiser` | `generate(text)` | `ChatterboxSynthesiser` (`CHATTERBOX_URL`, `CHATTERBOX_VOICE`) |
| `Audio::Player` | `play(buffer)` | `PortAudioPlayer` |
| `Commands::Parser` | `configure(config)`, `parse(text, context)` | `FallbackParser` over `RegexParser` then `OllamaParser` (`--parser hybrid`, the default); either alone with `--parser regex` or `ollama` |
| `Commands::Executor` | `configure(config)`, `discoverDevices()`, `discoverServices()`, `preview(command)`, `execute(command)` | `ExecutorRouter` over `HomeAssistant` and, with `SPOTIFY_URL`, `Spotify` |

A `Commands::Command` is `{domain, service, entity, data}`, plus an optional spoken `reply`
worded by the parser (a rule's `reply`, or the Ollama model's). The services a command may
name, and their fields, form a `Commands::ServiceCatalogue` (`src/commands/services.hpp`).
Nothing is hard-coded: the executor discovers the catalogue, and parsers receive it in
`Context::services` alongside the devices.

## Several executors

`ExecutorRouter` combines executors into one, so parsers see a single set of devices and
services. Each domain belongs to the first executor that offers it, and commands are routed by
domain; an executor that cannot be reached is skipped in discovery with a warning.

`Spotify` (`src/spotify/`) owns the `spotify` domain and talks to the Spotify control server
(`SPOTIFY_URL`), never to Spotify directly. It adds what Home Assistant's media players lack:
`cast` (move playback to a Spotify Connect device, discovered as `spotify.<device id>`),
`get_queue`, `request_song` and `time_until` (both with a `song` field). Play, pause and
volume stay with Home Assistant. Services that act on no device have
`ServiceFields::targetsEntity` false, and answers such as the queue come back in
`Result::reply`. Each service's `ServiceFields::description` lists how people ask for it, which
the Ollama prompt includes, and the shared phrase file has matching `spotify-*` rules.

## Service discovery

`HomeAssistant` builds the catalogue from `/api/services`, so adding or removing an
integration needs no configuration here:

- Only services that target entities are kept, with their fields (including those grouped
  in collapsible sections), required flags and selectors. Validation uses the selectors
  for number ranges and booleans.
- A device is controllable when its domain offers at least one catalogued service.
- Voice scope: security, system, notification, automation and helper domains (locks,
  alarms, cameras, updates, scripts, `input_*`, ...) are excluded by default. The home
  file's `voice_control.allow` and `voice_control.exclude` adjust that with domain or
  `domain.service` names.
- The catalogue is cached for a minute, as services change far less often than states.
  A command naming a service the cache lacks fetches it again (at most every five
  seconds), and a configuration change clears it. States are fetched for every command.

## Swapping a stage

- **Parser:** derive from `Commands::Parser` and construct it in `main.cpp` instead of
  `OllamaParser`. An LLM parser can describe `context.services` as tools, list
  `context.devices` in its prompt, and convert the chosen tool call into a `Command`.
  `Grammar::DeviceNames` can resolve a spoken device name if the model returns one.
  `OllamaParser` (`src/ollama/`) does this with a JSON schema instead of tools.
- **Executor:** derive from `Commands::Executor`. `discoverDevices()` and
  `discoverServices()` supply what parsers may target, and `execute()` must validate
  commands itself rather than trusting the parser.
- **Synthesiser:** derive from `Speech::Synthesiser` and construct it in `main.cpp` instead of
  `ChatterboxSynthesiser`. It returns a `Buffer` at any sample rate.
- **Transcriber:** derive from `Speech::Transcriber` and construct it in `main.cpp` instead
  of `WhisperTranscriber`. It receives mono float samples at `Audio::sampleRate`.

## Configuration

`Config::Configuration` reads `config/phrases.yaml`, merges the installation's
`config/home.yaml` over it when present, and passes the result to every consumer
through one callback: the executor reads `advantage_air` and `voice_control`, and
`RegexParser` reads `rules` and `aliases`, and `OllamaParser` reads `aliases`. When an edit is rejected, the previous configuration is applied again,
so the parser and executor never disagree.

## Side effects

- Parsing, `validateCommand`, and `controllableDevices` are pure.
- `discoverDevices()`, `discoverServices()` and `preview()` make read-only HTTP requests.
- Only `execute()` changes device state: one POST, never retried.
- After `execute()`, the worker says the outcome, which blocks it until playback ends. A speech
  failure is logged and never affects the command, which has already run.
- Constructors acquire resources: the microphone starts capture and Whisper loads its
  model. No constructor contacts Home Assistant.

## Remaining limits

- A command is one service call; multi-step requests need an API extension.
- Checking and posting are separate requests, so another client may change state in
  between. Relative volume has the same read/modify/write limit.
- Utterances are handled one at a time, so a slow command delays the next; more than three
  waiting utterances drop the oldest.
- Configuration, parsers and executors are single-threaded; only `CommandWorker` may use
  them while it exists.