# Smart-home voice commands

C++17 microphone recognition with whisper.cpp and PortAudio. Final English
transcripts go through the existing regex command parser and print the same
`Final:`, `Command recognized:`, or `No command recognized` messages as the
Moonshine prototype. Recognized commands are printed and posted to Home Assistant.

## Build and run

Install CMake, a C++17 compiler, pkg-config, PortAudio development files, and
nlohmann-json, and cpp-httplib headers. CMake downloads the pinned whisper.cpp v1.8.2 source on first
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

## Command flow

`Transcriber::transcribe` only converts audio into text. `processUtterance` parses
that text once, calls `printCommand`, then passes recognized request JSON to
`HomeAssistant::post`. Both silence detection and shutdown flushing use this same
path. `UtteranceBuffer` owns speech detection, preroll, and overflow recovery;
`main` connects capture to processing.

HTTP uses a reused client with timeouts, `Authorization: Bearer ...`, and JSON
content type. Set `HOME_ASSISTANT_TOKEN` before starting; a missing/empty token
produces a startup error. The host and port are configured near the top of `main.cpp`.
Service requests without a device are printed but not sent. Request failures and
conversation-agent errors are reported without retrying or stopping microphone capture.
At startup, `HomeAssistant::getStates()` queries `/api/states`, and the separate
`populateDeviceAliases` function fills `Parser::Options::devices`. Friendly names,
full entity IDs, and entity names with underscores replaced by spaces resolve to
media-player entity IDs. Both processing paths use these options. Duplicate normalized
aliases are blocked rather than choosing a device arbitrarily. Non-media-player
entities are ignored. Fetch/JSON errors or no media players stop startup, preventing
an accidental fallback to unresolved names. Restart to refresh the snapshot; the
population function replaces aliases while preserving other parser options.

## Parser

See [tests/parser.md](tests/parser.md) for supported phrases, output JSON, and optional
fuzzy device aliases. The app loads media-player aliases from your Home Assistant
instance at startup. This does not fetch Assist entity-registry aliases.

To build just the parser and tests without downloading Whisper or needing PortAudio:

```sh
cmake -S . -B build-parser -DSMARTHOME_BUILD_VOICE=OFF
cmake --build build-parser -j 4
ctest --test-dir build-parser --output-on-failure
```

References: [whisper.cpp](https://github.com/ggml-org/whisper.cpp),
[faster-whisper](https://github.com/SYSTRAN/faster-whisper).