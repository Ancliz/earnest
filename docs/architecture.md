# Architecture

```
capture thread:  Microphone → UtteranceBuffer → CommandWorker::submit
worker thread:   Speech::Transcriber → text
text + executor->discoverDevices() + discoverServices() → Commands::Parser → Commands::Command
Commands::Command → Commands::Executor → preview (read-only) or execute (one POST)
```

## Threads

- **Microphone capture thread** (`Microphone`): reads PortAudio into a five-second queue.
- **Main thread** (`listen` in `main.cpp`): reads that queue, splits speech into utterances
  and submits them. It never waits on inference or the network, so speech is still heard
  while a command runs.
- **Worker thread** (`CommandWorker`): transcribes, parses and executes each utterance in
  order, and reloads the phrase file every second while idle. It is the only thread that
  touches the transcriber, parser, executor and configuration, so none of them need locks.
  At most three utterances wait; beyond that the oldest is dropped. On shutdown it finishes
  every queued utterance before returning.

Each stage knows the others only through the types in `src/commands/command.hpp` and
`src/speech/transcriber.hpp`. `src/app/main.cpp` is the only place that names concrete
implementations.

| Interface | Methods | Current implementation |
| --- | --- | --- |
| `Speech::Transcriber` | `transcribe(samples)` | `WhisperTranscriber` |
| `Commands::Parser` | `configure(config)`, `parse(text, context)` | `OllamaParser` (`RegexParser` also available) |
| `Commands::Executor` | `configure(config)`, `discoverDevices()`, `discoverServices()`, `preview(command)`, `execute(command)` | `HomeAssistant` |

A `Commands::Command` is `{domain, service, entity, data}`. The services a command may
name, and their fields, form a `Commands::ServiceCatalogue` (`src/commands/services.hpp`).
Nothing is hard-coded: the executor discovers the catalogue, and parsers receive it in
`Context::services` alongside the devices.

## Service discovery

`HomeAssistant` builds the catalogue from `/api/services`, so adding or removing an
integration needs no configuration here:

- Only services that target entities are kept, with their fields (including those grouped
  in collapsible sections), required flags and selectors. Validation uses the selectors
  for number ranges and booleans.
- A device is controllable when its domain offers at least one catalogued service.
- Voice scope: security, system, notification, automation and helper domains (locks,
  alarms, cameras, updates, scripts, `input_*`, ...) are excluded by default. The phrase
  file's `voice_control.allow` and `voice_control.exclude` adjust that with domain or
  `domain.service` names.
- The catalogue is cached for a minute, as services change far less often than states.
  A command naming a service the cache lacks fetches it again (at most every five
  seconds), and a configuration change clears it. States are fetched for every command.

## Swapping a stage

- **Parser:** derive from `Commands::Parser` and construct it in `main.cpp` instead of
  `RegexParser`. An LLM parser can describe `context.services` as tools, list
  `context.devices` in its prompt, and convert the chosen tool call into a `Command`.
  `Grammar::DeviceNames` can resolve a spoken device name if the model returns one.
  `OllamaParser` (`src/ollama/`) does this with a JSON schema instead of tools.
- **Executor:** derive from `Commands::Executor`. `discoverDevices()` and
  `discoverServices()` supply what parsers may target, and `execute()` must validate
  commands itself rather than trusting the parser.
- **Transcriber:** derive from `Speech::Transcriber` and construct it in `main.cpp` instead
  of `WhisperTranscriber`. It receives mono float samples at `Audio::sampleRate`.

## Configuration

`Config::Configuration` reads `config/phrases.yaml` and passes it to every consumer
through one callback: the executor reads `advantage_air` and `voice_control`, and the parser reads `rules`
and `aliases`. When an edit is rejected, the previous configuration is applied again,
so the parser and executor never disagree.

## Side effects

- Parsing, `validateCommand`, and `controllableDevices` are pure.
- `discoverDevices()`, `discoverServices()` and `preview()` make read-only HTTP requests.
- Only `execute()` changes device state: one POST, never retried.
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