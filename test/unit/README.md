# ESP-IDF Target Unit Tests

This project runs psu-ext component tests using ESP-IDF's Unity menu runner.
The local `main/test_main.c` starts the menu without including ESP-IDF's four
example tests.

Production builds from the repository root do not compile component `test`
directories. This unit-test project enables tests with `TEST_COMPONENTS`.
The test app defaults select `esp32s3`, disable the task watchdog for the
interactive Unity menu, and use USB Serial/JTAG as the primary console so input
works on boards exposed as `USB JTAG/serial debug unit`.

## Build

```powershell
. C:\Espressif\tools\Microsoft.v6.1.PowerShell_profile.ps1
Set-Location D:\!Electronics\Projects\PSU-EXT-PRIVATE\software\psu-ext\test\unit
idf.py set-target esp32s3
idf.py build
```

`TEST_COMPONENTS` is a CMake cache variable. After a component is added to it,
run `idf.py fullclean` (or delete `build/`) so an existing build picks it up.

## Flash And Run

Replace `<PORT>` with the ESP32-S3 serial port, for example `COM7`.

```powershell
. C:\Espressif\tools\Microsoft.v6.1.PowerShell_profile.ps1
Set-Location D:\!Electronics\Projects\PSU-EXT-PRIVATE\software\psu-ext\test\unit
idf.py -p <PORT> flash monitor
```

In the monitor, press Enter to print the ESP-IDF Unity test menu. Run the smoke
test with any one of these inputs:

```text
"calibration infrastructure smoke"
[smoke]
*
```

The `*` command runs all registered tests.

The no-samples calibration timeout case injects a 100 ms semaphore wait only
for its capture task. It still exercises timeout cleanup; the production
capture deadline remains 30 seconds. The calibration SCPI test creates and
aborts a temporary staged point so earlier tests' committed calibration does
not affect its expected query response.

Other useful selectors:

| Selector | Covers |
|---|---|
| `[output]` | `OUTP` regression tests. These switch the CH1 relay; disconnect the load. |
| `[mqtt]` | `mqtt_svc` aggregation, formatting, payloads, filters, validation, and `SYST:MQTT:*` SCPI. |

Exit the monitor with `Ctrl+]`.

## Automated Serial Runner

The helper can run a tag selector and check the expected Unity summary:

```powershell
C:\Espressif\tools\python\v6.1\venv\Scripts\python.exe run_smoke_serial.py <PORT> --selector "[calibration]" --expect "calibration scpi reports transaction states and malformed commands:PASS" --summary "14 Tests 0 Failures 0 Ignored" --timeout 120
```

## Expected Smoke Result

The smoke case should report:

```text
calibration infrastructure smoke:PASS
```

The final Unity summary for only the smoke test should be:

```text
1 Tests 0 Failures 0 Ignored
OK
```

Observed on 2026-07-15 with `COM8`:

```text
calibration infrastructure smoke:PASS
1 Tests 0 Failures 0 Ignored
OK
```

## Expected Calibration Result

The `[calibration]` selector should report all target calibration tests passing.

Observed on 2026-07-15 with `COM8`:

```text
14 Tests 0 Failures 0 Ignored
OK
```

## Display Tests

Enable `PSU-EXT configuration > External I2C modules > SSD1306 128x64 OLED display`
in this test application's `idf.py menuconfig`, then build and select `[display]`
in the Unity runner. An attached display is not required for these tests.
When this option is disabled, display implementation and test sources are omitted.

## Pyrometer Tests

Enable `PSU-EXT configuration > External I2C modules > MLX90614 infrared object
temperature sensor` in this test application's menuconfig (with PSRAM enabled),
then build and select `[mlx90614]`. No physical sensor is required; driver and
PSRAM allocation failures are injected only during the relevant test case.
The display and pyrometer test groups are independently omitted when disabled.
