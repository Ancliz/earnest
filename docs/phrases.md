# Configuration and phrases

## Configuration files

The application reads two files:

- `config/phrases.yaml`: the phrase rules every installation shares.
- `config/home.yaml`: your own installation (aliases, `voice_control` and the optional
  `advantage_air` policy). Copy `config/home.example.yaml` to start; git ignores it. It is
  optional, and `--home` chooses another path.

The home file is merged over the phrase file: objects merge, lists append, and other values
replace. Save changes to either while it runs: it checks every 5 seconds while idle and before each command, and no
rebuild is needed. A malformed or invalid replacement keeps the previous configuration; the
first load must succeed. These are local, trusted files: they define what you authorise the
application to send.

The default parser, `OllamaParser`, needs neither phrase rules nor aliases: the model
matches what was said against the discovered device names and services. `rules` and
`aliases` drive `RegexParser`, the deterministic alternative selected with `--parser regex`.
`voice_control` and `advantage_air` work the same whichever parser you choose. The parser only
proposes a command; the Home Assistant executor then checks it against the voice scope and
the AC airflow policy, if one is configured, before sending anything, so neither parser can
get around them.

## Add a phrase

Phrase rules are read by `RegexParser`.

Find a rule with the intended domain/service, then add to `phrases`:

```yaml
- id: speaker-volume
  domain: media_player
  service: volume_set
  phrases:
    - '(set|change|adjust) [the] {device} volume to {volume}'
    - 'make [the] {device} volume {volume}'
  body:
    volume_level: '{volume}'
```

Syntax:

- `[the]` or `[the current]`: optional words. Optional sections cannot contain slots.
- `(set|change|adjust)`: alternatives, including alternatives containing spaces.
- `{device}`: a spoken friendly name or alias, resolved within the rule's domain.
- `{volume}`: 0–1 output; whole numbers/words mean percent, e.g. `forty` → 0.4.
- `{percent}`: 0–100 output, e.g. `forty percent` → 40.
- `{temperature}`: numeric degrees, including spoken numbers; unit limits and steps
  are checked against live HA attributes before sending.
- `{mode}` / `{option}`: a string. An optional `values` map converts synonyms.

`body` contains service fields, not `entity_id`; the parser supplies the resolved
ID. A placeholder must occupy the whole field value, so numeric captures stay
numbers, rather than becoming JSON strings. Each slot may appear once per phrase.
Rules can contain `phrases`, `regex`, or both. Existing speaker grammar is preserved
as editable regex rules with `device_group` and `value_group` capture indexes.
Noncapturing groups `(?:...)` do not affect those indexes. `value_type` is `volume`,
`percent`, or `temperature`; regex numeric output is also available as `{value}`.

An explicit `relative: true` on a media-player volume-up/down rule turns a spoken
amount into a `volume_up`/`volume_down` command with a normalised `amount` field.
The executor reads the current volume immediately before execution, clamps the
result to 0–1, and posts `volume_set`; an omitted amount moves 2%. A missing current
volume rejects the command. A rule may name any
service; whether Home Assistant offers it, and voice may use it, is checked against the
discovered catalogue when the command executes.

Rule order determines the first valid interpretation within a domain. Matches
that resolve to different commands across domains are rejected, so a speaker and
light both called “Kitchen” require a more specific alias. Unknown names, negations,
invalid numbers, and unsupported capabilities are rejected. Inputs are limited to
512 bytes; YAML files to 256 KiB and 256 rules. Regexes compile when config changes,
not for each audio block. The parser does not generate phrases at runtime.

## Aliases

Discovery creates domain-specific aliases from full entity IDs, readable entity
names, and `friendly_name`. Add preferred spoken names using entity IDs in `home.yaml`:

```yaml
aliases:
  climate.bedroom:
    - main bedroom
    - our bedroom
```

Only discovered entities receive these aliases. Duplicate normalized names within
a domain are blocked. Fuzzy matching is bounded and can be disabled with
`Commands::Context::fuzzyNames`. Discovery lists controllable entities; this is different
from Home Assistant's physical-device registry. Assist's separate registry aliases
are not fetched by this application.

## C++ API

```cpp
std::unique_ptr<Commands::Parser> parser = std::make_unique<RegexParser>();
std::unique_ptr<Commands::Executor> executor = std::make_unique<HomeAssistant>("http://homeassistant.local:8123", token);
Config::Configuration configuration(Config::defaultPhraseFile, [&](const Commands::json& candidate) {
    executor->configure(candidate);
    parser->configure(candidate);
}, Config::defaultHomeFile);

const auto command = parser->parse("set the bedroom temperature to twenty two", {executor->discoverDevices(), executor->discoverServices()});
// {"domain":"climate","service":"set_temperature","entity":"climate.bedroom","data":{"temperature":22}}

// Explicitly choose either a read-only preview or execution:
const auto preview = executor->preview(*command);
// const auto result = executor->execute(*command);
```

`Commands::Parser` defines `configure(configuration)` and `parse(text, context)`,
returning at most one `Commands::Command`. `RegexParser` compiles rules once and does
no file or network access; `configure` commits only after every rule compiles.
`OllamaParser` (`src/ollama/ollama_parser.hpp`) implements the same interface.

`Config::Configuration` performs file reads and reloads. `reloadIfChanged()` returns
true when changed text is applied and throws when it is rejected; every consumer then
keeps the previous configuration. Rejected text is not retried until a file changes.
While listening, configuration, parsing and execution all run on the `CommandWorker`
thread; synchronise externally if sharing these mutable objects between threads.

`validateCommand(command, devices, services, policy)` is pure: it resolves relative volume
and validates against the supplied device snapshot, `ServiceCatalogue` and `AirflowPolicy`. It returns a
`Commands::Result` with `success`, `error`, and the resolved `command`, and never throws.

`HomeAssistant::preview` queries fresh state, and uses the cached service catalogue, without posting.
`execute` uses that same preview before posting exactly once. It returns errors to the
caller without logging, retrying, substituting modes, or opening zones automatically.
The executor validates commands independently of the parser that supplied them.

## CLI tools

```sh
./build/earnest --check-phrases
./build/earnest --list-devices
./build/earnest --parse 'set the bedroom temperature to twenty two'
./build/earnest --phrases ./my-phrases.yaml --home ./my-home.yaml models/ggml-base.en.bin
```

`--check-phrases` needs no token/model/network. `--list-devices` and `--parse` use
read-only queries, load no model, and send no control requests. `--parse`
prints the proposed request, checks live capabilities and service availability,
and prints the resolved request or rejection.

Settings are taken from the most specific source first: command-line options, then the
ignored `.env` file in the current directory, then environment variables. Reading `.env`
does not change the environment.
Use `HOME_ASSISTANT_TOKEN` and optionally `HOME_ASSISTANT_URL` (full URL including
port, default `http://homeassistant.local:8123`), `OLLAMA_URL` (default
`http://localhost:11434`) and `OLLAMA_MODEL` (default `qwen3.5:4b`; `--ollama-model` overrides it) and `PARSER` (`ollama` or `regex`; `--parser` overrides it). See `.env.example`.

## Source layout

| Path | Responsibility |
| --- | --- |
| `src/app/main.cpp` | Implementation selection, modes and the capture loop |
| `src/app/command_worker.*` | Worker thread: transcribe, filter, parse, execute, reload configuration |
| `src/app/logging.*` | Console and file log sinks |
| `src/app/settings.*` | Arguments, connection settings, help |
| `src/commands/command.hpp` | `Command`, `Device`, `Context`, `Result`, and the `Parser`/`Executor` interfaces |
| `src/commands/services.hpp` | Service catalogue types, filled by executor discovery |
| `src/config/configuration.*` | YAML decoding, bounded file reads, and transactional reload |
| `src/speech/transcriber.hpp` | `Speech::Transcriber` interface |
| `src/speech/whisper_transcriber.*` | Whisper model lifetime and speech-to-text |
| `src/audio/microphone.*` | PortAudio capture and bounded capture queue |
| `src/audio/utterance_buffer.*` | Speech energy, silence boundaries, overflow recovery |
| `src/audio/audio.hpp` | Shared audio sample type and capture constants |
| `src/home_assistant/home_assistant.*` | Discovery, authenticated state/service queries, and posting |
| `src/home_assistant/control_policy.*` | Pure command resolution, capability checks, and AC airflow policy |
| `src/parser/regex_parser.*` | Regex backend and compiled rule snapshot |
| `src/parser/phrase_rules.*` | Phrase DSL, regex compilation, rule validation |
| `src/parser/device_names.*` | Domain-scoped exact/fuzzy name resolution |
| `src/parser/speech_text.*` | Text normalisation, spoken numbers, wake word and non-speech filters |
| `src/ollama/*` | Ollama client and LLM parser |
| `config/phrases.yaml` | Shared, editable phrase rules |
| `config/home.example.yaml` | Template for the untracked `config/home.yaml`: aliases, voice scope, AC policy |