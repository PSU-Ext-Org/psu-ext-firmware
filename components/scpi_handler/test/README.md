# SCPI compatibility checks

`command_aliases.json` captures the original spelling-to-callback mapping for
all 369 built-in spellings and 12 extension spellings. Keep this fixture
independent of the compact tables so accidental alias changes are detected.

From the repository root, run the actual C matcher and registry on a host:

```powershell
python tools/test_scpi_patterns.py --compiler '<path-to-host-cl.exe-or-clang.exe>'
```

MSVC requires C11 support and its x64 compiler. Espressif's embedded-only clang
does not include an x64 target. On POSIX hosts, omit `--compiler` to use `cc`.
The host harness substitutes string functions and a spinlock, links without heap
allocation, and checks alias mappings, grammar limits, atomic registration,
concurrent registration/dispatch, and capacity. It never accesses NVS or a board.

Build the existing `test/unit` application for target Unity tests. `[pattern]`
selects matcher tests; `[registry]` selects registration and dispatch regressions.
Retain the runner's NVS isolation wrappers for all target executions.

## Measured firmware size

Compared original source at `a616512` against this change using the same
ESP32-S3 production sdkconfig and ESP-IDF 6.1 environment. Values come from
`esp_idf_size --format json2` on the two linker maps; total image size includes
the new matcher and registration code.

| Measurement | Original | Compact patterns | Change |
| --- | ---: | ---: | ---: |
| Total image | 1,175,876 | 1,171,040 | -4,836 bytes |
| Flash code | 805,134 | 806,778 | +1,644 bytes |
| Flash data | 254,716 | 248,236 | -6,480 bytes |
| Static RAM data and BSS, including RTC | 59,355 | 59,371 | +16 bytes |

The registry adds a four-byte pattern bitset; the linked BSS grows by 16 bytes
with alignment. Dispatch uses bounded stack arrays and does not retain expanded
aliases. Both firmware applications built successfully and all host checks
passed. Target execution was unavailable because COM5 could not be opened
(access denied); no test firmware was flashed.
