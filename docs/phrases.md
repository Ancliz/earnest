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

An explicit `relative: true` on a media-player rule delegates a captured amount to
`/conversation/process` with the original text. An omitted optional amount uses
the direct service. HVAC commands are never delegated to a conversation agent.

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
HomeAssistant assistant("http://wyvern:8123", std::getenv("HOME_ASSISTANT_TOKEN"));
auto states = assistant.getStates();
auto devices = findAvailableDevices(states); // Available controllable entities.
Parser::Options options;
populateDeviceAliases(states, options);
auto id = Parser::resolveEntityId("AC", "climate", options); // String or null.

Parser::Engine phrases("config/phrases.yaml");
phrases.reloadIfChanged();
auto request = phrases.parse("set master bedroom temperature to twenty two", options);
// {"endpoint":"/services/climate/set_temperature",
//  "body":{"entity_id":"climate.ac_master_bed","temperature":22}}
```

`Engine::reload()` throws on invalid config and retains the old snapshot.
`reloadIfChanged()` returns true only when a new snapshot was accepted; inspect
`reloadError()` after failure. `addPhrase(ruleId, phrase)` appends a validated
in-memory phrase. Save the phrase in YAML to persist it; the next file reload
replaces in-memory additions. Engine mutation/parsing is used on the application's
main thread; synchronize externally if sharing an Engine between threads.

`Parser::parse(text, options)` remains a convenience wrapper around the default
file. The default path is set to the source project's `config/phrases.yaml` at build time;
pass `--phrases FILE` or construct an Engine with another path to override it.

`validateCommand(request, states, configuration)` returns an empty string when a
request is permitted, or an explanation. `HomeAssistant::post(request, configuration)`
fetches fresh state, applies that validation, checks service availability, then
posts once. There are no automatic retries, mode substitutions, or zone-opening
sequences. Errors do not trigger a fallback through Assist.

## CLI tools

```sh
./build/voice-test --check-phrases
./build/voice-test --list-devices
./build/voice-test --parse 'set master bedroom temperature to twenty two'
./build/voice-test --phrases ./my-phrases.yaml models/ggml-base.en.bin
```

`--check-phrases` needs no token/model/network. `--list-devices` and `--parse` use
read-only state queries, load no model, and send no control requests. `--parse`
prints the proposed request and any policy rejection. Environment variables take
precedence over values in the ignored `.env` file in the current directory.
Use `HOME_ASSISTANT_TOKEN` and optionally `HOME_ASSISTANT_URL` (full URL including
port, default `http://wyvern:8123`).

## Source layout

| Path | Responsibility |
| --- | --- |
| `src/app/main.cpp` | CLI dispatch, Whisper transcription, command processing, and debug transcript printing |
| `src/app/settings.*` | Arguments, environment loading, help |
| `src/audio/microphone.*` | PortAudio capture and bounded capture queue |
| `src/audio/utterance_buffer.*` | Speech energy, silence boundaries, overflow recovery |
| `src/audio/audio.hpp` | Shared audio sample type and capture constants |
| `src/home_assistant/home_assistant.*` | Authenticated HTTP state/service queries and posting |
| `src/home_assistant/device_aliases.*` | Entity discovery and alias population |
| `src/home_assistant/control_policy.*` | Live capability checks and verified AC airflow policy |
| `src/parser/parser.*` | Public parsing API, active rule snapshot, and reloads |
| `src/parser/detail.hpp` | Shared internal parser declarations |
| `src/parser/device_names.cpp` | Domain-scoped exact/fuzzy name resolution |
| `src/parser/speech_text.cpp` | Text normalization and spoken numbers |
| `src/parser/phrase_rules.cpp` | Phrase DSL, regex compilation, rule validation |
| `src/parser/config_yaml.cpp` | YAML decoding |
| `config/phrases.yaml` | User-editable phrases, aliases, and installation policy |