# Parser regression suite

The original speaker phrase variants and JSON request format are covered by
`parser_test.cpp`. Device discovery/ambiguity is covered by `device_aliases_test.cpp`.
YAML loading, user phrases, hot reload, domain resolution, and AdvantageAir checks
are covered by `phrase_config_test.cpp`.

See [the phrase API/configuration guide](../docs/phrases.md) and
[the AdvantageAir research and policy](../docs/advantage_air.md).

```sh
cmake -S . -B build-parser -DSMARTHOME_BUILD_VOICE=OFF
cmake --build build-parser -j 4
ctest --test-dir build-parser --output-on-failure
```
