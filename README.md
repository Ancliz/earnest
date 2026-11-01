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
./build/voice-test models/ggml-base.en.bin --threads 4 --threshold 0.007 --silence-ms 700 --cpu
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

The default build is CPU-only: without a GPU backend compiled in, transcription runs on the
CPU whether or not `--cpu` is given. To use a GPU, configure with one backend and rebuild:

- NVIDIA: `-DGGML_CUDA=ON` (needs the CUDA toolkit; on Arch, `cuda`, with `nvcc` in
  `/opt/cuda/bin`).
- Other GPUs: `-DGGML_VULKAN=ON` (needs the Vulkan headers and `shaderc`).

CMake caches these options, so switch one off explicitly (`-DGGML_VULKAN=OFF`) rather than
just leaving it out. In a GPU build, `--cpu` forces CPU transcription at runtime.

## Commands and discovery

See [the phrase guide](docs/phrases.md) for the YAML format and C++ API, and
[the AdvantageAir notes](docs/advantage_air.md) for the optional zoned-AC safety policy.
Copy `config/home.example.yaml` to `config/home.yaml` for your own aliases, voice scope and
AC policy; it is not tracked by git. Edit `config/phrases.yaml` to add phrases; valid changes
reload while the app runs, without recompiling. An invalid replacement keeps the
last valid configuration.

```sh
./build/voice-test --list-devices
./build/voice-test --check-phrases
./build/voice-test --parse 'set the bedroom temperature to twenty two'
```

The discovery and dry-run modes send no control commands and require no Whisper
model. Dry runs query both states and service availability. Copy `.env.example` to `.env` (ignored by git) and set `HOME_ASSISTANT_TOKEN`; the
Home Assistant and Ollama URLs and the Ollama model are set there too. Environment
variables override the file.

The pipeline has three swappable stages: a `Speech::Transcriber` turns audio into
text, a `Commands::Parser` turns text into one `Commands::Command`, and a
`Commands::Executor` discovers devices and runs commands. `WhisperTranscriber`,
`RegexParser`, and `HomeAssistant` are the current implementations, selected in
`src/app/main.cpp`. Parsing cannot post commands, and the executor validates every
command independently of the parser that produced it.

`Config::Configuration` owns file reads and transactional hot reload for both the
parser and the executor. `HomeAssistant::preview` performs read-only live-state and
service validation; `execute` performs the same checks and posts once. Relative
volume is resolved against fresh state and sent as `volume_set`; without an amount it moves by 2%.

See [the architecture notes](docs/architecture.md) for the interfaces and how to
swap each stage.

## Parser tests

To build just the parser and tests without downloading Whisper or needing PortAudio:

```sh
cmake -S . -B build-parser -DSMARTHOME_BUILD_VOICE=OFF
cmake --build build-parser -j 8
ctest --test-dir build-parser --output-on-failure
```

References: [whisper.cpp](https://github.com/ggml-org/whisper.cpp),
[faster-whisper](https://github.com/SYSTRAN/faster-whisper).

## Project layout

- `src/app/`: startup, CLI/connection settings, implementation selection, and reporting.
- `src/commands/`: the command vocabulary, service catalogue types, and parser/executor interfaces.
- `src/config/`: bounded YAML decoding, file access, and transactional reload.
- `src/speech/`: the transcriber interface and Whisper implementation.
- `src/audio/`: microphone capture and utterance buffering.
- `src/parser/`: the regex parser, phrase DSL, spoken numbers, and device-name matching.
- `src/home_assistant/`: the Home Assistant executor, discovery, and control policy.
- `src/ollama/`: Ollama client and the pending LLM parser.
- `config/phrases.yaml`: shared, editable phrase rules.
- `config/home.example.yaml`: template for the untracked `config/home.yaml` (aliases, voice scope, AC policy).
- `tests/` and `docs/`: automated checks and supporting documentation.

## API documentation

Every class, type, function, and method has a Doxygen comment. Public contracts live
in headers; private helpers and test callbacks are described beside their
implementations.

With Doxygen installed, generate the browsable reference from the repository root:

```sh
doxygen docs/Doxyfile
```

Open `build/docs/html/index.html`. Generated HTML/XML stays in the ignored build tree.