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

Optional native ESP-IDF driver for a 128x64 SSD1306 OLED. Connect SDA to GPIO6,
SCL to GPIO7, common ground, and power appropriate to the module. The bus uses
I2C controller 1 at 400 kHz, independently of the ADC on controller 0. Use
external pull-ups to 3.3 V suitable for 400 kHz; internal pull-ups are enabled
as well.

`ssd1306_init()` is called after measurement sampling starts. It probes 0x3C,
then 0x3D, and uses the first responding address. An ACK assumes a compatible
OLED; it cannot identify the controller. Initialization uses the same C8/A1
orientation as the reference's `tiny4koled_init_128x64r` configuration.
It returns `true` on successful startup and `false` on absence or initialization
failure. The component handles diagnostic logging and cleanup internally;
`main` only logs successful display detection.

Six rows use a compact 3x5 font with four-pixel character spacing in the left
64 pixels. The right half shows a scrolling output-voltage chart.
Text rows are spaced eight pixels apart, in this order:

```text
RELAY: OFF
V0: 15.0000V
V1: 12.3456V
I1: 0.1234A
P1: 1.5234W
ADC: 128 SPS
```

Relay status is the cached CH1 output command, not physical contact feedback.
The ADC row shows the currently configured conversion rate in samples per second,
read every refresh so runtime rate changes are reflected. This is the ADC rate,
not the per-channel sampling rate. An unavailable rate shows `----`.
Measurements are calibrated averages from the existing service, including CH0
input voltage and CH1 derived power. Values refresh every 200 ms and follow the
service's averaging settings, like SCPI reads. Unavailable readings show `----`.
Each update replaces the entire left row, including trailing blanks, without
writing into the chart. Four decimal places are retained.

The chart uses the same averaged CH1 voltage as the numeric reading. It keeps
63 points at 200 ms intervals (approximately 12.6 seconds), with the newest
point on the right. The vertical range starts at zero and rounds the highest
visible value up to whole volts, with a minimum 1 V range. Its header shows
the current range, for example `V1 0-13V`. Unavailable readings leave gaps;
the display does not backfill history from before startup.

Detection happens only once per boot. Absence or initialization failure is
nonfatal and releases I2C resources. A runtime I2C failure stops the display
task and releases the bus; reconnecting requires a reboot. Repeated init calls
return the original result and never restart the task. Initialization is intended
for the single startup caller, not concurrent callers.

## Hardware verification

- Check orientation and all four measurements against SCPI with a stable input.
- Toggle CH1 output and verify the relay row switches between ON and OFF.
- Confirm text stays in the left half and the voltage chart stays in the right.
- Vary output voltage and check scrolling, range changes, and a flat zero trace.
- Check zero, fractional leading zeros, and transitions to shorter values.
- Boot without the OLED and confirm normal sampling and communications.
- Disconnect the OLED while running and confirm sampling and protection continue.
- Attach it after startup and confirm it remains inactive until reboot.
- Test modules at either supported address; if both respond, 0x3C takes priority.
