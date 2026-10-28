# Architecture

```
Microphone → UtteranceBuffer → Speech::Transcriber → text
text + executor->discoverDevices() → Commands::Parser → Commands::Command
Commands::Command → Commands::Executor → preview (read-only) or execute (one POST)
```

Each stage knows the others only through the types in `src/commands/command.hpp` and
`src/speech/transcriber.hpp`. `src/app/main.cpp` is the only place that names concrete
implementations.

| Interface | Methods | Current implementation |
| --- | --- | --- |
| `Speech::Transcriber` | `transcribe(samples)` | `WhisperTranscriber` |
| `Commands::Parser` | `configure(config)`, `parse(text, context)` | `RegexParser` |
| `Commands::Executor` | `configure(config)`, `discoverDevices()`, `preview(command)`, `execute(command)` | `HomeAssistant` |

A `Commands::Command` is `{domain, service, entity, data}`. The services a command may
name, and their fields, are listed in `Commands::serviceCatalog()` (`src/commands/services.hpp`).

## Swapping a stage

- **Parser:** derive from `Commands::Parser` and construct it in `main.cpp` instead of
  `RegexParser`. An LLM parser can describe `serviceCatalog()` as tools, list
  `context.devices` in its prompt, and convert the chosen tool call into a `Command`.
  `Grammar::DeviceNames` can resolve a spoken device name if the model returns one.
  `src/ollama/ollama_parser.hpp` declares that parser for implementation.
- **Executor:** derive from `Commands::Executor`. `discoverDevices()` supplies the devices
  parsers may target, and `execute()` must validate commands itself rather than trusting
  the parser.
- **Transcriber:** derive from `Speech::Transcriber` and construct it in `main.cpp` instead
  of `WhisperTranscriber`. It receives mono float samples at `Audio::sampleRate`.

## Configuration

`Config::Configuration` reads `config/phrases.yaml` and passes it to every consumer
through one callback: the executor reads `advantage_air`, and the parser reads `rules`
and `aliases`. When an edit is rejected, the previous configuration is applied again,
so the parser and executor never disagree.

## Side effects

- Parsing, `validateCommand`, and `controllableDevices` are pure.
- `discoverDevices()` and `preview()` make read-only HTTP requests.
- Only `execute()` changes device state: one POST, never retried.
- Constructors acquire resources: the microphone starts capture and Whisper loads its
  model. No constructor contacts Home Assistant.

## Remaining limits

- A command is one service call; multi-step requests need an API extension.
- Checking and posting are separate requests, so another client may change state in
  between. Relative volume has the same read/modify/write limit.
- Transcription, discovery, parsing and execution run sequentially; slow inference or
  network calls can overflow the five-second capture queue.
- Configuration and parsers are single-threaded; synchronize before sharing them.