# Regression suites

| Suite | Covers |
| --- | --- |
| `parser_test.cpp` | Speaker phrase variants, spoken numbers, device names, aliases and ambiguity |
| `resolver_test.cpp` | Rooms, physical devices, shared names and how ties are settled |
| `control_policy_test.cpp` | Discovery, multi-domain parsing, command validation, and AdvantageAir checks |
| `configuration_test.cpp` | YAML limits, hot reload, rejected edits, and rollback across consumers |
| `home_assistant_test.cpp` | Preview and execution against a local mock Home Assistant |
| `utterance_buffer_test.cpp` | Utterance segmentation and discard recovery |
| `settings_test.cpp` | Command-line modes and connection settings |

See [the phrase API/configuration guide](../docs/phrases.md) and
[the AdvantageAir research and policy](../docs/advantage_air.md).

```sh
cmake -S . -B build-parser -DSMARTHOME_BUILD_VOICE=OFF
cmake --build build-parser -j 4
ctest --test-dir build-parser --output-on-failure
```