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

An explicit `relative: true` on a media-player volume-up/down rule produces an
internal `/commands/media_player/adjust_volume` intent with `entity_id` and a
signed, normalized `amount`. The executor reads the current volume immediately
before execution, clamps the result to 0–1, and posts `volume_set`. A missing
current volume rejects the command. An omitted optional amount uses the direct
volume-up/down service. Arbitrary `/conversation/process` requests are rejected.

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
`Options::fuzzyDevices`. Discovery lists controllable entities; this is different
from Home Assistant's physical-device registry. Assist's separate registry aliases
are not fetched by this application.

## C++ API

```cpp
std::unique_ptr<Commands::Parser> parser = std::make_unique<Parser::RegexParser>();
Config::Configuration configuration("config/phrases.yaml", [&](const Commands::json& candidate) {
    validateControlConfiguration(candidate);
    parser->configure(candidate);
});
HomeAssistant assistant("http://wyvern:8123", token);
Commands::Executor& executor = assistant;

const auto states = assistant.getStates();
const auto context = buildCommandContext(states);
const auto command = parser->parse("set master bedroom temperature to twenty two", context);
// {"endpoint":"/services/climate/set_temperature",
//  "body":{"entity_id":"climate.ac_master_bed","temperature":22}}

// Explicitly choose either a read-only preview or execution:
const auto preview = executor.check(command, configuration.data());
// const auto result = executor.execute(command, configuration.data());
```

`Commands::Parser` defines `configure(configuration)` and `parse(text, context)`.
The regex implementation accepts a decoded configuration, compiles rules once,
and does no file/network access. `configure` commits only after all rules compile.
An LLM adapter implements the same interface; selecting it does not change the
transcriber or executor. No LLM adapter is included yet.

`Config::Configuration` performs file reads and reloads. Its application callback
validates control policy before configuring the selected parser. `reload()` throws
on an invalid candidate; `reloadIfChanged()` retains the previous configuration and
reports failure via `error()`. Call it explicitly from the app's timer/before parsing.
`RegexParser::addPhrase(ruleId, phrase)` adds a validated in-memory phrase; save it
in YAML to persist it. The next configuration update replaces in-memory additions.
Configuration and parsing run on the main thread; synchronize externally if sharing
these mutable objects between threads.

The hidden global `Parser::parse` convenience wrapper and file-owning `Engine`
were removed. Use an explicit parser instance. `Config::defaultFile()` supplies
the source project's `config/phrases.yaml` path, or pass `--phrases FILE`.

`prepareCommand(request, states, configuration)` is pure: it resolves relative
volume and validates against supplied snapshots. It returns an `ExecutionResult`
with `success`, `error`, and the resolved `request`. `validateCommand` returns its
error string for callers only interested in validation. Invalid request/state/policy
JSON returns a rejection instead of escaping as a JSON exception.

`HomeAssistant::check` queries fresh state and service availability without posting.
`execute` uses that same check before posting exactly once. It returns errors to the
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
| `src/app/main.cpp` | Backend selection, explicit stage orchestration, and reporting |
| `src/commands/command.hpp` | Parser/executor interfaces, context, and execution results |
| `src/config/configuration.*` | YAML decoding, bounded file reads, and transactional reload |
| `src/speech/transcriber.hpp` | Whisper model lifetime and speech-to-text |
| `src/app/settings.*` | Arguments, local connection settings, help |
| `src/audio/microphone.*` | PortAudio capture and bounded capture queue |
| `src/audio/utterance_buffer.*` | Speech energy, silence boundaries, overflow recovery |
| `src/audio/audio.hpp` | Shared audio sample type and capture constants |
| `src/home_assistant/home_assistant.*` | Authenticated HTTP state/service queries and posting |
| `src/home_assistant/device_aliases.*` | Entity discovery and alias population |
| `src/home_assistant/control_policy.*` | Pure command resolution, capability checks, and AC airflow policy |
| `src/home_assistant/services.hpp` | Shared domain/service/field catalog |
| `src/parser/parser.*` | Regex backend and compiled rule snapshot |
| `src/parser/detail.hpp` | Shared internal parser declarations |
| `src/parser/device_names.cpp` | Domain-scoped exact/fuzzy name resolution |
| `src/parser/speech_text.cpp` | Text normalization and spoken numbers |
| `src/parser/phrase_rules.cpp` | Phrase DSL, regex compilation, rule validation |
| `config/phrases.yaml` | User-editable phrases, aliases, and installation policy |