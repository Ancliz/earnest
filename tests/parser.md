# Speaker command parser

`Parser::parse(text)` returns a JSON command or `null`. Device names are captured from
structural boundaries, with no predefined device list required:

```cpp
#include "parser.hpp"

auto command = Parser::parse("set the volume of the living room speaker to forty");
// {"endpoint":"/services/media_player/volume_set",
//  "body":{"entity_id":"living room speaker", "volume_level":0.4}}
```

Supported examples (determiners and polite prefixes/suffixes are optional):

| Control | Examples |
| --- | --- |
| Absolute volume | set the volume of kitchen to forty; set kitchen volume to 40%; kitchen volume 40; set volume to forty on kitchen; turn kitchen to 0.4 |
| Relative volume | raise volume on kitchen by ten; lower kitchen volume by five; turn kitchen up; turn the kitchen volume down; make kitchen quieter; volume up on kitchen |
| Mute | mute kitchen; silence kitchen; unmute the sound on kitchen; kitchen mute |
| Playback | play music on kitchen; resume kitchen; pause kitchen; kitchen pause; stop playback in kitchen; toggle music on kitchen |
| Tracks | next song on kitchen; skip this song; skip to the next track on kitchen; previous track on kitchen; go back one song in kitchen |
| Power | turn on kitchen; switch kitchen off; power off kitchen |

Names can contain multiple words. A leading `the` and a trailing possessive `'s`
are removed from the captured name. Case, whitespace, sentence punctuation and
hyphenated number words are normalized. The aliases `volum`, `volumn`, `volome`
and `paus` accommodate a few common transcription errors without globally changing
words inside device names.

Whole numbers (digits or English words from zero to one hundred) are percentages:
`1` means 1%, `forty two` means 42%. Decimal levels `0.4`, `.4`, `1.0`,
`zero point four`, and `point four` use the normalized 0–1 scale. Numeric decimals
above one are percentages, so `12.5` means 12.5%. An explicit `percent`, `per cent`,
or `%` always selects percentages. `half`, `full`, `maximum`, and `minimum` are
also supported. Invalid levels and negative values return `null`.

Targets can be omitted (`pause`, `volume forty`, `turn volume up`); their JSON
`body.entity_id` is `null` unless `options.defaultDevice` supplies an entity ID.
Fill it before sending a service request; do not omit it and risk targeting all players.
The parser does not execute commands or infer device availability.

## Optional device aliases and fuzzy matching

```cpp
Parser::Options options;
options.devices = {
    {"kitchen", "media_player.kitchen"},
    {"living room speaker", "media_player.lounge"},
    {"lounge", "media_player.lounge"}
};
auto command = Parser::parse("pause kichen", options);
// {"endpoint":"/services/media_player/media_pause",
//  "body":{"entity_id":"media_player.kitchen"}}
```

With aliases configured, only recognized targets are accepted. Exact matches win;
otherwise bounded Levenshtein distance permits one edit for names of at least four
characters, or two when both names have at least eight characters. Equally close
matches to different entities return `null`. Multiple aliases for the same entity
are allowed. Set `options.fuzzyDevices = false` to require exact normalized names.
Fuzzy matching handles spelling errors, not phonetic similarity or intent inference.

## Output contract

Every successful result has `endpoint` and `body`. Endpoints are relative to your
Home Assistant `/api` base URL. All requests use POST with JSON:

```cpp
const auto request = Parser::parse("pause kitchen", options);
if(!request.is_null()) {
    const string endpoint = "/api" + request["endpoint"].get<string>();
    const string body = request["body"].dump();
    // POST endpoint with body, Content-Type: application/json, and your auth header.
}
```

Direct requests use `/services/media_player/<service>`. The body contains `entity_id`
and, where applicable, `volume_level` (0–1) or `is_volume_muted` (boolean). Without
an alias map, `entity_id` remains the captured spoken name: resolve it to an actual
entity ID before sending. The parser does not invent Home Assistant IDs from names.
Set `options.defaultDevice` to an entity ID to handle omitted service targets.

Relative commands without an amount use `volume_up`/`volume_down` and the player's
default step. Explicit amounts ("turn kitchen up by ten percent") instead return:

```json
{
  "endpoint": "/conversation/process",
  "body": {"text": "turn kitchen up by ten percent", "language": "en"}
}
```

This delegates the change to Assist's conversation agent without fetching current
volume in C++. `body.text` preserves the original utterance. Optionally set
`options.conversationAgent` to include an `agent_id`; otherwise Home Assistant uses
its default agent. Only locally recognized, valid relative commands are delegated;
unknown or invalid commands still return `null`.

The conversation agent resolves spoken names independently: expose appropriate
names/aliases to Assist. Local fuzzy aliases and `defaultDevice` do not rewrite the
forwarded text. An omitted target needs context on the conversation side. Inspect
the response's `response.response_type` and error data; an HTTP 200 does not by itself
mean the agent recognized or executed the command. This endpoint handles text intent
processing, not the complete audio/STT/TTS pipeline. No network request is performed
by the parser.

References: [REST API](https://contributors.home-assistant.io/core/internals/rest-api/),
[Conversation API](https://contributors.home-assistant.io/core/internals/conversation-api/).

Regexes compile once and match whole utterances. Inputs longer than 512 bytes,
negations, and compound commands using `and`, `then`, or `but` are rejected rather
than partially executed. This also reserves those words in device names. Names
containing standalone `to` or `by` are reserved as volume delimiters. Only one
command is supported per utterance. With no alias list, arbitrary words after
`play` are treated as a device name; selecting songs, artists, playlists, and
streaming services is outside this grammar.

## Run regression checks

From the project root:

```sh
g++ -std=c++17 -Wall -Wextra -Wpedantic parser.cpp tests/parser_test.cpp -o /tmp/moonshine-parser-test
/tmp/moonshine-parser-test
```
