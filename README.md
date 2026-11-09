# Earnest

A voice assistant that lives in your house and stays there.

Say the wake word, ask for what you want ("turn the lounge speaker up a bit", "set the bedroom to
twenty two") and it takes care of it. Speech is heard by [whisper.cpp](https://github.com/ggml-org/whisper.cpp),
understood by a local [Ollama](https://ollama.com) model, and carried out through
[Home Assistant](https://www.home-assistant.io). Everything runs on your own machines: no cloud
account, no audio leaving the network. Editable phrase rules handle the phrasings they list
exactly, and the model handles the rest. If you'd rather not run a language model, or are
unable to, the phrase rules can work alone (set `PARSER=regex` in `.env`, or run with
`--parser regex`).

It learns your home rather than the other way round. Devices and the functionalities they have are
discovered from Home Assistant, so a new speaker or light is ready to talk to as soon as Home
Assistant knows about it. Every request is checked against the device's live state before
anything is sent, so a misheard word is far more likely to be politely declined than to do
something you didn't ask for.

Controlling the home is where it starts. It is built as a set of swappable stages so that, over
time, it can grow into a more general helper: something closer to a good butler than a remote
control.

## Getting started

### Requirements

- CMake, a C++17 compiler and pkg-config.
- Development files for PortAudio, nlohmann-json, libyaml, cpp-httplib and dr_libs (`dr_wav.h`).
- A Home Assistant server and a [long-lived access token](https://www.home-assistant.io/docs/authentication/#your-account-profile).
- An Ollama server with a model installed (default `qwen3.5:4b`).
- A microphone that supports mono float32 at 16 kHz.

CMake downloads the pinned whisper.cpp v1.8.2 source on first configuration; no Python runtime
is needed.

### Build

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
ctest --test-dir build --output-on-failure

# Download the English base model (~142 MiB):
bash build/_deps/whisper-src/models/download-ggml-model.sh base.en "$PWD/models"
```

The default build transcribes on the CPU. A GPU build is recommended: it is much faster, and in
practice it has transcribed commands more accurately with the same model. To use a GPU,
configure with one backend and rebuild:

- NVIDIA: `-DGGML_CUDA=ON` (needs the CUDA toolkit; on Arch, `cuda`, with `nvcc` in
  `/opt/cuda/bin`).
- Other GPUs: `-DGGML_VULKAN=ON` (needs the Vulkan headers and `shaderc`).

CMake caches these options, so switch one off explicitly (`-DGGML_VULKAN=OFF`) rather than just
leaving it out. In a GPU build, `--cpu` forces CPU transcription at runtime.

To use an existing whisper.cpp checkout instead of downloading it, add
`-DFETCHCONTENT_SOURCE_DIR_WHISPER=/path/to/whisper.cpp` when configuring. Use GGML `.bin`
models.

### Configure

1. Copy `.env.example` to `.env` and set `HOME_ASSISTANT_TOKEN`. The same file sets the Home
   Assistant and Ollama addresses and the Ollama model; it overrides environment variables, and command-line options override both.
2. Optionally copy `config/home.example.yaml` to `config/home.yaml` for your own aliases, voice
   scope and zoned-AC policy. Both files are ignored by git.

### Run

```sh
./build/earnest models/ggml-base.en.bin --wake-word "hey earnest"
```

Check things without speaking or changing anything:

```sh
./build/earnest --check-phrases     # validate the configuration files
./build/earnest --list-devices      # what it can control
./build/earnest --parse 'set the bedroom temperature to twenty two'   # dry run
```

`--list-devices` and `--parse` make read-only requests and need no Whisper model. Run
`--help` for every option, including `--parser`, `--ollama-model`, `--no-prerequest` (turns off a speed-up specific to `qwen3.5:4b`; use it with any other model), `--threads`, `--threshold`, `--silence-ms`, `--log-level`
and `--log-file`.

## How it listens

- **Utterances:** simple energy detection keeps 200 ms of leading audio and ends an utterance
  after 700 ms of silence (`--silence-ms`). Raise `--threshold` if room noise triggers it, or
  lower it for quiet speech. Utterances over 15 seconds, or interrupted by a capture overflow,
  are discarded rather than run incomplete.
- **Wake word:** with `--wake-word`, only speech that opens with it is acted on. Matching is
  fuzzy, so "hey Travis"  for a wake work of "Jarvis" still counts, and an optional greeting
  such as "hey" or "ok" is allowed.
- **Noise:** Whisper segments with low confidence are dropped, and transcripts that are only
  fillers or sound labels ("mhm", "[laughter]") are ignored.
- **In the background:** commands run on their own thread, so it keeps listening while one is
  carried out. Ctrl+C finishes what you've already said before stopping.

## What it can control

Devices and services come from Home Assistant, cached for a minute and refreshed when a new
service appears. Security, system, automation and helper domains (locks, alarms, cameras,
updates, scripts and the like) are left out of voice control by default; `voice_control` in
`config/home.yaml` adjusts that.

Before anything is sent, the command is checked against the device's live state: the service
and its fields must exist, numbers must be in range, and a zoned AC system must keep enough
airflow (see [the AdvantageAir notes](docs/advantage_air.md)). Relative volume ("up by 5", or
just "up", which moves 2%) is calculated from the current level.

With a Spotify control server running, set `SPOTIFY_URL` in `.env` to cast playback to a Spotify
Connect device, hear what's queued, request a song, or ask how long until a song plays. Playback
and volume stay with Home Assistant.

## Tests

To build the libraries and tests without Whisper or PortAudio:

```sh
cmake -S . -B build-parser -DSMARTHOME_BUILD_VOICE=OFF
cmake --build build-parser -j"$(nproc)"
ctest --test-dir build-parser --output-on-failure
```

Tests use recorded Home Assistant responses and a loopback mock server; they never contact a
real installation.

## Documentation

- [Architecture](docs/architecture.md): the pipeline stages, threads, service discovery and how
  to swap each stage.
- [Phrases and configuration](docs/phrases.md): the configuration files, the regex phrase
  format, aliases and the C++ API.
- [Zoned AC policy](docs/advantage_air.md): the optional airflow guard for ducted systems.

Every class, type, function and method has a Doxygen comment. With Doxygen installed, run
`doxygen docs/Doxyfile` from the repository root and open `build/docs/html/index.html`.

## Project layout

- `src/app/`: startup, settings, logging, implementation selection and the command worker.
- `src/audio/`: microphone capture, utterance buffering, WAV decoding and playback.
- `src/speech/`: the transcriber and synthesiser interfaces, with Whisper and Chatterbox implementations.
- `src/ollama/`: the Ollama client and LLM parser.
- `src/spotify/`: the Spotify executor, a client of the Spotify control server.
- `src/parser/`: the regex and fallback parsers, phrase rules, spoken numbers, wake word and name matching.
- `src/commands/`: the shared command, device and service-catalogue types and interfaces, and the executor router.
- `src/home_assistant/`: the Home Assistant executor, discovery and control policy.
- `src/config/`: YAML decoding and configuration reloads.
- `config/`: shared phrase rules, and the template for your installation file.
- `tests/` and `docs/`: automated checks and supporting documentation.