# Phrase configuration and API

The application reads `config/phrases.yaml`. Save changes while it runs: it checks once a
second and again before parsing a completed transcript. No C++ rebuild is needed.
A missing, malformed, or invalid replacement keeps the previous complete ruleset
and airflow policy. The first load must succeed. This is a local, trusted config
file: it defines the commands you authorize the application to send.

## Add a phrase

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
amount into a `volume_up`/`volume_down` command with a normalized `amount` field.
The executor reads the current volume immediately before execution, clamps the
result to 0–1, and posts `volume_set`. A missing current volume rejects the command.
An omitted optional amount uses the direct volume-up/down service. Services outside
`Commands::serviceCatalog()` are rejected.

Rule order determines the first valid interpretation within a domain. Matches
that resolve to different commands across domains are rejected, so a speaker and
light both called “Kitchen” require a more specific alias. Unknown names, negations,
invalid numbers, and unsupported capabilities are rejected. Inputs are limited to
512 bytes; YAML files to 256 KiB and 256 rules. Regexes compile when config changes,
not for each audio block. The parser does not generate phrases at runtime.

## Aliases

Discovery creates domain-specific aliases from full entity IDs, readable entity
names, and `friendly_name`. Add preferred spoken names using entity IDs:

```yaml
aliases:
  climate.ac_master_bed:
    - master bedroom
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
std::unique_ptr<Commands::Executor> executor = std::make_unique<HomeAssistant>("http://wyvern:8123", token);
Config::Configuration configuration(Config::defaultPhraseFile, [&](const Commands::json& candidate) {
    executor->configure(candidate);
    parser->configure(candidate);
});

const auto command = parser->parse("set master bedroom temperature to twenty two", {executor->discoverDevices()});
// {"domain":"climate","service":"set_temperature","entity":"climate.ac_master_bed","data":{"temperature":22}}

// Explicitly choose either a read-only preview or execution:
const auto preview = executor->preview(*command);
// const auto result = executor->execute(*command);
```

`Commands::Parser` defines `configure(configuration)` and `parse(text, context)`,
returning at most one `Commands::Command`. `RegexParser` compiles rules once and does
no file or network access; `configure` commits only after every rule compiles. An LLM
parser implements the same interface (see `src/ollama/ollama_parser.hpp`).

`Config::Configuration` performs file reads and reloads. `reloadIfChanged()` returns
true when changed text is applied and throws when it is rejected; every consumer then
keeps the previous configuration. Rejected text is not retried until the file changes.
Configuration and parsing run on the main thread; synchronize externally if sharing
these mutable objects between threads.

`validateCommand(command, devices, policy)` is pure: it resolves relative volume and
validates against the supplied device snapshot and `AirflowPolicy`. It returns a
`Commands::Result` with `success`, `error`, and the resolved `command`, and never throws.

`HomeAssistant::preview` queries fresh state and service availability without posting.
`execute` uses that same preview before posting exactly once. It returns errors to the
caller without logging, retrying, substituting modes, or opening zones automatically.
The executor validates commands independently of the parser that supplied them.

## CLI tools

```sh
./build/voice-test --check-phrases
./build/voice-test --list-devices
./build/voice-test --parse 'set master bedroom temperature to twenty two'
./build/voice-test --phrases ./my-phrases.yaml models/ggml-base.en.bin
```

`--check-phrases` needs no token/model/network. `--list-devices` and `--parse` use
read-only queries, load no model, and send no control requests. `--parse`
prints the proposed request, checks live capabilities and service availability,
and prints the resolved request or rejection. Environment variables take
precedence over values in the ignored `.env` file in the current directory.
Reading that file returns connection settings and does not call `setenv`.
Use `HOME_ASSISTANT_TOKEN` and optionally `HOME_ASSISTANT_URL` (full URL including
port, default `http://wyvern:8123`).

## Source layout

| Path | Responsibility |
| --- | --- |
| `src/app/main.cpp` | Implementation selection, stage orchestration, and reporting |
| `src/app/settings.*` | Arguments, local connection settings, help |
| `src/commands/command.hpp` | `Command`, `Device`, `Context`, `Result`, and the `Parser`/`Executor` interfaces |
| `src/commands/services.hpp` | Shared domain/service/field catalog |
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
| `src/parser/speech_text.*` | Text normalization and spoken numbers |
| `src/ollama/*` | Ollama client and the pending LLM parser declaration |
| `config/phrases.yaml` | User-editable phrases, aliases, and installation policy |