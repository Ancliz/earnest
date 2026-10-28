# Smart-home voice commands

C++17 microphone recognition with whisper.cpp and PortAudio. English transcripts go through the regex command parser and recognized commands
are posted to Home Assistant. Transcript and parsed-request printing is currently
debug output, handled by `debugPrintTranscript`. Speech capture and parsing do not
depend on that output.

## Build and run

Install CMake, a C++17 compiler, pkg-config, PortAudio development files, and
nlohmann-json, libyaml development files, and cpp-httplib headers. CMake downloads the pinned whisper.cpp v1.8.2 source on first
configuration; no Python runtime is needed.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j 4
ctest --test-dir build --output-on-failure

# Download the English base model (~142 MiB), then run from this directory:
bash build/_deps/whisper-src/models/download-ggml-model.sh base.en "$PWD/models"
# Set HOME_ASSISTANT_TOKEN in your environment before starting.
./build/voice-test models/ggml-base.en.bin
```

The model is not bundled. To use an existing whisper.cpp checkout without downloading
source, add `-DFETCHCONTENT_SOURCE_DIR_WHISPER=/path/to/whisper.cpp` when configuring.
Use GGML `.bin` models, not the Moonshine ONNX models or faster-whisper model folders.

```sh
./build/voice-test --help
./build/voice-test models/ggml-base.en.bin --threads 4 --threshold 0.01 --silence-ms 700 --cpu
```

The microphone must support mono float32 at 16 kHz. Capture runs separately from
inference, buffering up to five seconds. Simple RMS energy detection retains 200 ms
of leading audio, then submits an utterance after 700 ms of silence. Increase
`--threshold` if room noise repeatedly triggers recognition; lower it for quiet
speech. This energy detector is not a trained VAD and needs adjustment in noisy
rooms. Utterances longer than 15 seconds and audio interrupted by buffer overflow
are discarded until the next silence, rather than executing incomplete commands.
Ctrl+C flushes the current buffered utterance and releases audio/model resources.
Any audio still queued behind it when stopping is discarded.

GPU use depends on how whisper.cpp was built; the default build supports CPU.
For NVIDIA acceleration, configure with `-DGGML_CUDA=ON`; for Vulkan,
`-DGGML_VULKAN=ON`. `--cpu` disables GPU use at runtime.

## Commands and discovery

See [the phrase guide](docs/phrases.md) for the YAML format and C++ API, and
[the AdvantageAir notes](docs/advantage_air.md) for the verified installation and
control constraints. Edit `config/phrases.yaml` to add phrases and aliases; valid changes
reload while the app runs, without recompiling. An invalid replacement keeps the
last valid configuration.

```sh
./build/voice-test --list-devices
./build/voice-test --check-phrases
./build/voice-test --parse 'set master bedroom temperature to twenty two'
```

The discovery and dry-run modes send no control commands and require no Whisper
model. Dry runs query both states and service availability. Set `HOME_ASSISTANT_TOKEN` in the environment or ignored `.env` file. Optionally
set `HOME_ASSISTANT_URL` to a full URL including port.

`HomeAssistant::getStates` fetches state; `findAvailableDevices` filters controllable
entities; `buildCommandContext` returns domain-specific maps; `Commands::resolveEntityId`
looks up a spoken name using those maps. Friendly-name normalization is shared.
`Commands::Parser` is the backend interface; `Parser::RegexParser` implements it.
`WhisperTranscriber::transcribe` returns text only. The application then obtains a
device snapshot, calls `parser->parse(text, context)`, prints debug output, and
explicitly calls `executor.execute(command, configuration)`. Parsing cannot post
commands. Another parser backend can be selected at the composition root without
changing audio capture or execution.

`Config::Configuration` owns file reads and transactional hot reload. Parser
configuration changes are explicit; parsing never reloads files or uses a shared
global engine. `HomeAssistant` implements `Commands::Executor`: `check` performs
read-only live-state and service validation; `execute` performs those same checks
and posts once. Both return structured results, leaving reporting to the app.
Relative volume amounts are resolved against fresh state and sent through the
ordinary volume service. Arbitrary conversation requests are rejected.

See [the architecture audit](docs/architecture.md) for the reviewed concerns,
changes, side-effect boundaries, and remaining limitations.

## Parser tests

To build just the parser and tests without downloading Whisper or needing PortAudio:

```sh
cmake -S . -B build-parser -DSMARTHOME_BUILD_VOICE=OFF
cmake --build build-parser -j 4
ctest --test-dir build-parser --output-on-failure
```

References: [whisper.cpp](https://github.com/ggml-org/whisper.cpp),
[faster-whisper](https://github.com/SYSTRAN/faster-whisper).

## Project layout

- `src/app/`: startup, CLI/connection settings, orchestration, and reporting.
- `src/commands/`: parser/executor contracts and command context/results.
- `src/config/`: bounded YAML decoding, file access, and transactional reload.
- `src/speech/`: Whisper transcription.
- `src/audio/`: microphone capture and utterance buffering.
- `src/parser/`: phrase parsing, YAML decoding, and text/device-name matching.
- `src/home_assistant/`: HTTP client, device discovery, and control policy.
- `config/phrases.yaml`: editable phrases, aliases, and installation policy.
- `tests/` and `docs/`: automated checks and supporting documentation.

Small application-only reporting helpers live in `src/app/main.cpp`. Parser implementation
helpers share `src/parser/detail.hpp`; separate headers remain for shared module
interfaces and classes with substantial implementations.

## API documentation

Functions and methods use Doxygen comments for behavior, parameters, return values,
errors, ownership, and side effects. Public contracts live in headers; private
helpers and test callbacks are described beside their implementations.

With Doxygen installed, generate the browsable reference from the repository root:

```sh
doxygen docs/Doxyfile
```

Open `build/docs/html/index.html`. Generated HTML/XML stays in the ignored build tree.
