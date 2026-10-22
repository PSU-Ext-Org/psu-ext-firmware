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

# SSD1306 measurement display

Enable **PSU-EXT configuration > External I2C modules > SSD1306 128x64 OLED display**
in menuconfig. Connect SDA GPIO6, SCL GPIO7, common ground, and module-appropriate
power. External pull-ups to 3.3 V are required; internal I2C pull-ups are disabled.
The manager owns I2C controller 1; the ADC remains on controller 0. The display
uses 400 kHz alone or 100 kHz when sharing the bus with an enabled MLX90614.
It probes 0x3C then 0x3D once at startup and assumes the first ACK is an
SSD1306-compatible 128x64 OLED. Orientation remains C8/A1.

## Views and SCPI

The module owns and registers these commands before the shared bus is created:

```text
EXT:DISPL:VIEW 1
EXT:DISPL:VIEW 2
EXT:DISPL:VIEW?
```

`EXT:DISPLAY:VIEW` and `EXT:DISPLAY:VIEW?` are aliases. Keywords are case-insensitive.
The setter accepts a registered positive decimal view ID and sends no response
on success. The built-in IDs are `1` and `2`; leading zeroes, signs, unknown IDs,
and overflowing numbers are rejected. The query
accepts no arguments and returns the selected number. Invalid arguments leave the
selection unchanged. Valid commands return `ERR,"Display unavailable"` before
startup completes, if the display is absent, or after an I2C failure. Compiling
SSD1306 out removes its commands, rendering code, and button handling entirely.

View 1 starts after every reboot; selection is not written to flash. It shows
large CH1 output voltage, current, and power, followed by smaller CH0 input
voltage. `REL` is right-aligned beside V1; its commanded `ON`/`OFF` state
(not physical contact feedback) is right-aligned beside I1. These use the large
font. Unusually large V1/I1 values fall back to the small font to preserve all
digits without overlapping relay status. Below VIN, small-font CH1 OVP and OCP
set values occupy fixed left/right columns, followed by their latched trip
indicators (`OK`/`TRIP`, or `----` if unavailable) on the last row. Limit values
retain four decimals directly after the colon, without padding or unit suffixes
(e.g. `OVP:25.0000`, `OCP:2.5000`). Labels and value starts stay fixed in their columns. Measurements
retain four decimal places. Unavailable readings show `----`. There is no chart.

View 2 uses two 64-pixel columns, with these rows:

| Row | Left | Right |
| --- | --- | --- |
| 1 | USB host mounted | ADC conversion rate in SPS |
| 2 | USB CDC DTR asserted | Voltage averaging count |
| 3 | | Current averaging count |
| 4 | WiFi DHCP connected | Power averaging count |
| 5 | SCPI TCP session active | |
| 6 | WiFi credentials set/not set | |

WiFi connected means an IP address has been obtained, not just association.
TCP active means a client accepted on SCPI port 5025; it clears on WiFi/IP loss,
client closure, or transport failure. USB mounted means the host enumerated the
device; CDC open means DTR is asserted. Suspension alone does not unmount USB
or close the CDC port. Credential presence uses the existing manager policy:
both SSID and password must be nonempty. No credential contents are displayed.
ADC rate is the conversion rate, not per-channel sampling frequency. Averaging
counts are the configured counts for voltage, current, and power individually.

Live values refresh every 200 ms. Every update rewrites all eight pages from a
1 KB task-owned framebuffer, so switching views clears old content. Selection
is synchronized across SCPI and BOOT; only the display task accesses I2C.

## BOOT button

GPIO0 is an active-low input with an internal pull-up and interrupts on both
edges. The ISR captures level and edge time and notifies the display task.
The task debounces press and release for 40 ms without periodic GPIO polling;
it sleeps until an edge, debounce deadline, or display refresh is due. Each press selects the next registered view and wraps to the first
(1 -> 2 -> 1 with the built-in catalog). Holding does not repeat. A button held when
the display task starts is ignored until released. BOOT controls views only
while the display is operational; its reset-time bootloader function is unchanged.

## Failures and hardware checks

Initialization is nonfatal, cached, and not retried until reboot. A runtime I2C
failure marks the display unavailable, stops its task and removes its GPIO0 interrupt handler, and
removes only its device. The shared bus remains owned by the manager.

- Verify four decimal places, maximum/zero values, relay state, and both layouts.
- Switch views by USB SCPI, TCP SCPI, and BOOT; check query results and clearing.
- Test bounced, held, repeated, and startup-held button presses.
- Change ADC rate and all three averaging counts and check live updates.
- Check WiFi IP acquisition/loss separately from TCP connection/closure.
- Check USB enumeration separately from serial-terminal DTR open/close.
- Check credential set/clear state without showing SSID/password contents.
- Boot without OLED and disconnect it at runtime; confirm normal sampling,
  protection, communications, and unavailable command responses continue.
- Boot with support disabled; display commands must be unknown.

The tests in `ssd1306/test/` register an additional view with ID 17 from its own
source file. They cover command validation, registration errors, callback context,
frame clearing, selection and cycling across noncontiguous IDs, debounce,
timer wraparound, concurrent selectors, framebuffer bounds, maximum values,
and full-frame clearing without needing an attached OLED.

## Adding a view

The hardware task handles I2C, refresh timing, and button events. The view registry
handles SCPI selection and BOOT cycling without knowing which views exist.
The layout keeps extension APIs separate from implementation details:

```text
ssd1306/
  include/                     # Public view extension APIs (enabled builds only)
    ssd1306_views.h             # Frame type and registration contract
    ssd1306_draw.h              # Shared drawing helpers
  private/                     # Core headers; not exported to other components
    ssd1306.h                  # Hardware initialization
    ssd1306_views_internal.h    # Selection, SCPI, debounce, task rendering
  core/
    ssd1306.c                  # Hardware, refresh task, button ISR
    ssd1306_views.c             # Registry, selection, SCPI, debounce
    ssd1306_draw.c              # Clipped text drawing
  views/
    ssd1306_builtin_views.c     # Explicit startup registration catalog
    ssd1306_view_measurements.c # Service reads and layout for view 1
    ssd1306_view_status.c       # Service reads and layout for view 2
    private/
      ssd1306_builtin_views.h   # Built-in declarations and layout-test data
  sources.cmake                # Shared source lists for firmware and tests
```

CMake exports `ssd1306/include` only when the display is enabled. It adds
`ssd1306/private` and `ssd1306/views/private` with `PRIV_INCLUDE_DIRS`, so other
components cannot include hardware or selection internals. Tests explicitly add
those paths privately to test the implementation without broadening its API.

Add a source file such as `views/ssd1306_view_example.c`:

```c
#include "ssd1306_views.h"
#include "ssd1306_draw.h"

static void draw_example(ssd1306_frame_t frame, void *context)
{
    (void)context;
    /* Read this view's services here, then draw into the cleared frame. */
    ssd1306_draw_text(frame, 0, 0, 1, SSD1306_WIDTH, "EXAMPLE");
}

esp_err_t ssd1306_register_example_view(void)
{
    const ssd1306_view_t view = {
        .id = 3,
        .draw = draw_example,
    };
    return ssd1306_views_register(&view);
}
```

Then:

1. Add the file to `ssd1306/sources.cmake`. This list is shared by enabled
   firmware builds and the dedicated view tests.
2. Declare the registration function in `views/private/ssd1306_builtin_views.h`,
   and add it to the `register_views` array in `views/ssd1306_builtin_views.c` after the existing
   entries. Registering explicitly keeps the source linked from the component
   archive; no constructors or implicit self-registration are used.
3. Add any new service dependencies inside the SSD1306 condition in the module
   `CMakeLists.txt` (and to the test component when used there). Dependencies
   link privately after component registration because IDF configuration flags
   are unavailable during early dependency discovery. The application must also
   include any newly required service in its component graph; current services
   are already required by `main`.

`EXT:DISPL:VIEW 3`, its query, and BOOT cycling then work without changing the
hardware task, selector, or existing views. IDs need not be contiguous. The
first registered view is the boot default; registration order defines BOOT order.
Keep IDs stable because clients select views by ID.

The fixed registry holds up to `SSD1306_MAX_VIEWS` (currently 8) and allocates no
heap memory. It copies each descriptor. Zero IDs and null callbacks are invalid;
duplicate IDs and capacity exhaustion return errors without changing the registry.
There is no unregister operation. Register built-ins during startup before adding
other views; the catalog retains progress if registration fails. The registration
API itself is synchronized with selection and rendering.

Drawing callbacks run only on the display task, outside the registry lock. They
receive a cleared 1 KB framebuffer and an optional borrowed context pointer that
must remain valid until reboot. Each view may use its own data structure; the
built-in snapshot in `views/private/ssd1306_builtin_views.h` is only for built-in layout tests.
Read services through their synchronized APIs, keep work bounded to preserve
refresh/button responsiveness, and let the hardware task write the framebuffer
to I2C. Do not retain the frame pointer or perform I2C from a view.

Production builds with SSD1306 disabled omit every view, the registry, drawing
helpers, button handling, and display-specific dependency declarations. The target
test application also omits display tests and their dependencies when disabled.
Enable SSD1306 in the test application menuconfig to build the [display] tests;
these tests use synthetic snapshots and do not require an attached display.

Test sources live beside each module under its own `test/` directory. The parent
`i2c_1_modules/test/CMakeLists.txt` conditionally registers enabled modules' tests.
