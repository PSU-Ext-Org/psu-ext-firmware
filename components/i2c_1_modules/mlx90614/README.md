<!--
Copyright 2026 PSU-EXT Authors

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
-->

# MLX90614 pyrometer

Enable `PSU-EXT configuration > External I2C modules > MLX90614 infrared object
temperature sensor` in `idf.py menuconfig` (PSRAM must be enabled). The module is
disabled by default. Polling defaults to 100 ms and can be set to 100-10000 ms.
`MLX90614 temperature averaging samples` sets the scalar-query window from 1 to
25 successful polls, defaulting to 10. Set it to 1 for no averaging. This is a
build-time menuconfig setting.

## Hardware and lifecycle

Connect one sensor at address `0x5A` to the manager-owned I2C_1 bus on SDA6/SCL7,
with common ground and external pull-ups compatible with the board's 3.3 V I/O.
The module borrows the existing bus; it never installs or deletes a bus driver.
When enabled, both the pyrometer and optional SSD1306 use 100 kHz, the MLX90614
SMBus limit. Display-only builds retain 400 kHz. The ADC bus is unaffected.

Startup failure is nonfatal and cached until reboot; other optional modules
still initialize. Commands register before probing and report unavailable when
startup fails. Sampling starts after a conservative 250 ms power-up allowance.
Read errors (including PEC/CRC errors) invalidate the scalar reading without
appending a sample or destroying history. Polling continues and the next valid
reading restores scalar availability. Module error logs occur on state
transitions; ESP-IDF retains its own bus-timeout diagnostics.

The host polling interval does not change the sensor's internal refresh rate.
Existing FIR/IIR filters, factory calibration, and emissivity are left untouched.
No firmware calibration, SCPI averaging controls, ambient-temperature queries,
second object channel, display changes, or MQTT integration are added.

## Commands and wire format

| Command | Result |
|---|---|
| `EXT:MEAS:TEMP? CH1` | Averaged object temperature, Celsius with four decimals |
| `EXT:MEAS:TEMP:DATA? CH1[,count[,start_offset]]` | Timestamped binary history |

`1` and case-insensitive `CH1` select the same object channel. `MEASURE` and
`TEMPERATURE` are accepted in place of `MEAS` and `TEMP`, in any combination.
Other channels, signs on counts, overflow, and extra arguments are rejected.

Temperature is signed end to end: `-13.1500` is a valid scalar response.
Native 0.02 K/count values convert exactly to Celsius x 10000 as
`raw * 200 - 2731500`; the sensor error flag rejects a reading. Four decimal
places preserve protocol consistency, not a claim of extra sensor resolution.
Scalar queries average the newest configured number of successful polls, using
all available samples while history is shorter than the window. The signed
average is rounded once to Celsius x 10000, with ties away from zero. Queries do
not acquire additional readings. A failed poll still makes the scalar unavailable;
after recovery, averaging includes retained successful polls from before the
failure until they leave the window. Binary history retains individual readings.

History contains at most 1000 successful polls. A full ring overwrites its
oldest record. This retains approximately 100 seconds at the default rate;
identical successive readings are retained because the sensor has no sample ID.
Counts default to 1000 and clamp to retained capacity. Offset zero addresses the
oldest retained sample, matching the current voltage/current implementation;
records are returned oldest first. Zero count, empty history, or an offset beyond
retained samples returns the empty block `#10`. History is a non-destructive,
coherent snapshot; offsets do not remain stable while a full ring advances.

Binary framing is the existing SCPI definite-length format `#<digits><bytes>`.
Each eight-byte record contains:

- Four-byte little-endian **unsigned** ESP timer timestamp in milliseconds,
  wrapping modulo 2^32.
- Four-byte little-endian **signed** two's-complement Celsius x 10000 value.

For example, Python decodes a record with `struct.unpack('<Ii', record)`;
divide the second result by 10000. There is no binary line terminator.
A full response is 8006 bytes, within the current 8192-byte USB TX limit.

## Storage, ownership, and optional builds

The ring (8000 bytes) and binary response buffer (8016 bytes) use explicit
PSRAM-capable allocation with no internal-RAM fallback. State, task stack, locks,
and the library's small device handle use internal memory. Locks are initialized
before command registration publishes their context. History locking covers
sampling and snapshots; transport callbacks run after releasing that lock.
A separate response lock protects shared response bytes across USB/TCP callbacks.
If another history response owns that lock, a query immediately returns
`ERR,"Temperature history busy"`; the client can retry after that response ends.
Scalar queries and sampling remain independent of the response lock.
All resources remain module-owned. Startup allocation/task failures release
buffers and the device handle; lock handles remain valid for registered commands.

Disabling the option excludes the module, vendored communication driver,
commands, test sources, and private include paths. The main measurement enums,
voltage/current histories, calibration, and their command handlers are unchanged.
Temperature commands are unknown in a disabled build.

## Library provenance

Communication uses the MIT-licensed `larryli/mlx90614` component v1.1.0:
https://components.espressif.com/components/larryli/mlx90614/versions/1.1.0

Vendored from upstream commit `2e05d0592d98124e14b59b10c008d145b785b58b`:
https://github.com/larryli/esp-idf_mlx90614/tree/2e05d0592d98124e14b59b10c008d145b785b58b

The source, public header, and full MIT license are retained under `vendor/`.
Local changes: replace infinite I2C transfer waits with 50 ms waits, explicitly
include `<stdlib.h>`, and reproduce the upstream MIT notice in the source/header.
The header also has an inclusion guard. Read transfer failures return without
an additional vendor-driver log; the polling module logs failure/recovery
transitions. Lower-level ESP-IDF I2C diagnostics retain their existing policy.
The library adds a device to the supplied ESP-IDF master bus and checks SMBus PEC.
Only its checked raw-read API is used for temperature acquisition. No local
replacement communication protocol or sensor EEPROM writes are introduced.
Vendoring lets CMake omit the driver completely when menuconfig disables it.

## Validation

Enable the module in the target-test application's menuconfig and run
`[mlx90614]`. Tests use synthetic samples and fault injection, without installing
a real sensor. They cover signed conversion/formatting and averaging, binary
encoding, ring wrap, count/offset validation, aliases, runtime errors/recovery, concurrency,
PSRAM placement, and allocation-failure cleanup/cached startup failure. Additional
regressions hold a transport callback open to check immediate busy responses and
stable response bytes, and inject transfers through the real vendor read path
to check quiet failures, unchanged output on error, PEC validation, and recovery.

`../test/check_build.py <build-directory> --nm <target-nm-executable>` verifies
source and ELF inclusion/exclusion for both optional modules. Build all four
combinations of SSD1306 and MLX90614 to validate their independence.
Hardware checks should exercise both modules concurrently, USB/TCP retrieval,
negative-temperature readings where practical, and disconnection/reconnection.
