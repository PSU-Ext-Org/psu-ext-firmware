# SCPI command modules

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
