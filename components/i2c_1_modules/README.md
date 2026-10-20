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

# External I2C modules

Select modules under **PSU-EXT configuration > External I2C modules** in
`idf.py menuconfig`. SSD1306 support is disabled by default; when disabled,
its driver, view registry, drawing helpers, and view sources are not compiled.
Display-specific dependency declarations are also omitted. MLX90614 is listed as not implemented and
has no selectable option or driver yet.

The startup call `ESP_ERROR_CHECK_WITHOUT_ABORT(i2c_1_modules_init())` runs
after measurement sampling starts. With no modules selected, it returns
`ESP_OK` without creating a bus. Otherwise it creates controller 1 on SDA
GPIO6/SCL GPIO7 with internal pull-ups disabled and glitch filter count 7.
Use suitable external pull-ups to 3.3 V, common ground, and module-appropriate
power. The SSD1306 device operates at 400 kHz. Controller 0 remains owned by
the measurement service.

Each implemented module's menuconfig option must `select PSUEXT_I2C_1_ENABLED`.
This hidden flag guards shared bus initialization independently of any particular
module. Module source inclusion and initialization stay guarded by their own
options. When adding another driver, select the same flag; the manager will
create one shared bus whenever any supported module is enabled. The MLX90614
placeholder does not select it.

The manager owns the shared bus until reboot. Drivers borrow its handle and
clean up only their own devices. Initialization returns the first error;
module failures must not prevent other selected modules from being attempted
or abort boot. Bus creation failure prevents module initialization. Repeated
calls return the cached startup result without retrying, and calls must be
serialized by the startup task. Runtime display failure stops only the display
task and removes its device; the bus stays available until reboot.

See [SSD1306 details](ssd1306/README.md) for rendering, hardware checks, and
adding independently registered views.

## Module-owned SCPI commands

The shared SCPI registry is available to any component. Define callbacks and a
`static const scpi_handler_command_t` table in the module's own source, then call
`scpi_handler_register_commands()` during module initialization. Add
`scpi_handler` to that component's `PRIV_REQUIRES`. The dispatcher does not need
to know which module owns the commands. SSD1306 defines its own `EXT:DISPL:VIEW` setter and query; see its documentation.

For example, a future module could register its own identity query:

```c
#include "scpi_handler.h"

static void identify(const char *argument,
                     const scpi_handler_response_writer_t *writer,
                     void *context)
{
    (void)context;
    if (argument != NULL) {
        writer->write_text("ERR,\"Query takes no arguments\"");
        return;
    }
    writer->write_text("Example module");
}

static const scpi_handler_command_t commands[] = {
    {"I2C1:EXAMPLE:IDN?", identify, NULL},
};

/* Call once from the module initializer; propagate registration failures. */
static esp_err_t register_commands(void)
{
    return scpi_handler_register_commands(
        commands, sizeof(commands) / sizeof(commands[0]));
}
```

Registration copies the entries into a fixed 32-command registry without heap
allocation. Keyword strings must remain immutable and callback context must remain valid
until reboot. Keywords may contain at most 127 characters.
Tables register atomically: invalid entries, duplicate keywords, or insufficient
capacity leave the registry unchanged. Do not register the same table twice;
module initialization should cache its result. There is no unregister operation.

Keywords are uppercase exact spellings, with `?` included for queries. Register
short and long aliases as separate entries. Incoming keywords are matched without
case sensitivity, and argument case is preserved. Existing built-in commands take
priority; extensions cannot override them. Use a module-specific namespace such
as `I2C1:<module>:` to avoid conflicts.

Callbacks receive the current transport writer for USB or TCP. Argument and writer
pointers are borrowed for the call only; do not retain them. Check `write_binary`
before sending binary data. Callbacks execute outside the registry lock and may
run concurrently across transports, so protect mutable module state separately.
Registration is safe while transports are active; a command arriving before its
registration receives the normal unknown-command response.

Register only for compiled-in modules. A module may register after successful
hardware initialization, or register earlier and report an unavailable device in
its callbacks. Runtime device failure must leave callbacks safe, since command
registrations persist until reboot.
