# SCPI command modules

`*IDN?` returns `MaxEELabs,PSU-EXT,PE_<MAC>,<firmware-version>`, preserving the four
comma-separated manufacturer, model, serial-number, and firmware-revision fields.
The revision comes from `g_firmware_version`, defined once in `main/main.c`.
Other components can depend on `firmware_info` and include `firmware_version.h`
to use the same constant. The unit runner supplies a `unit-test` identity.
`<MAC>` is all six bytes of the chip's factory eFuse base MAC, formatted as 12
uppercase hexadecimal digits without separators. The serial is stable across
reboots and firmware updates and does not depend on configured interface MACs.

Each built-in module exports a `scpi_handler_command_module_t` backed by a static
name/callback table. Callbacks receive the parsed command and the complete text
and binary response writer. Add a handler and table entry for a command; register
a new family once in `SCPI_BUILTIN_MODULES` in `scpi_handler.c` (and add its source
to CMake). Built-ins take precedence over extensions and use none of the 32
extension slots. Tables list exact aliases, including optional leading colons
only where previously accepted.

Private protocol dependencies are separated into `scpi_handler_arguments.h`,
`scpi_handler_responses.h`, and `scpi_handler_commands.h`. The comma reader
consumes mutable tokens, trims each token, and rejects empty fields. Callers must
check `scpi_handler_args_end` to reject trailing fields. Boolean word support and
a trailing semicolon are explicit options: output/timer accept only `0/1`,
protection also accepts `ON/OFF`, and REL permits one trailing semicolon.

Compatibility: ADC-rate and averaging-count queries intentionally ignore extra
arguments. Scalar measurement queries require a channel; calibration transaction
queries reject extra arguments. Tests record these differences; this refactor
does not tighten command syntax.

DATA publishes its mutex under an initialization spinlock. Allocation and use of
both PSRAM buffers occur while holding that mutex, including the binary writer
callback. Allocation failure releases the mutex and leaves initialization
retryable. Writers must consume/copy the buffer synchronously and must not
recursively invoke a DATA command.

REL storage lives in `measure_svc_relative.c`. The coordination guard holds the
same mutex as calibration transitions, from state inspection through the entire
REL or protection operation. Calibration start clears REL while holding it.
The lock order is coordination before protection/measurement; guarded callers
must not call calibration APIs. Legacy calibration protection guards remain as
compatibility wrappers.

Tests run through `test/unit`. `[arguments]` needs no service initialization;
`[registry]` checks dispatch/alias contracts; `[rel]` covers REL commands and
service behavior; calibration/protection concurrency remains an integration
test. `[data]` exercises simultaneous DATA queries; run it first after boot to
exercise lazy initialization. Building the test application does not run tests.

`MEAS:VOLT:DATA?` and `MEAS:CURR:DATA?` apply the same current channel REL offsets
as scalar queries, clamping each exported value at zero. `MEAS:POWER?` and
`MEAS:POWER:DATA?` derive corrected power from each original CH1 V/I pair using
those two offsets; there is no power REL command. Scalar power averages corrected
products using the power averaging count. Both zero offsets restore absolute
power behavior. Changing/resetting REL also changes queries of existing history.
DATA timestamps, chronological order, oldest-relative pagination, and binary
8-byte little-endian timestamp/value records are unchanged. Coordination/history
locks are released before invoking the response writer.
