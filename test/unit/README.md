# ESP-IDF Target Unit Tests

This project configures ESP-IDF's supplied Unity test application for psu-ext
component tests. The runner comes from:

```powershell
$env:IDF_PATH\components\unity\test_apps\main
```

Production builds from the repository root do not compile component `test`
directories. This unit-test project enables tests with `TEST_COMPONENTS`.
The test app defaults select `esp32s3`, disable the task watchdog for the
interactive Unity menu, and use USB Serial/JTAG as the primary console so input
works on boards exposed as `USB JTAG/serial debug unit`.

## Build

```powershell
. C:\Espressif\tools\Microsoft.v6.0.PowerShell_profile.ps1
Set-Location D:\!Electronics\Projects\PSU-EXT-PRIVATE\software\psu-ext\test\unit
idf.py set-target esp32s3
idf.py build
```

## Flash And Run

Replace `<PORT>` with the ESP32-S3 serial port, for example `COM7`.

```powershell
. C:\Espressif\tools\Microsoft.v6.0.PowerShell_profile.ps1
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

Exit the monitor with `Ctrl+]`.

## Automated Serial Runner

The helper can run a tag selector and check the expected Unity summary:

```powershell
C:\Espressif\tools\python\v6.0\venv\Scripts\python.exe run_smoke_serial.py <PORT> --selector "[calibration]" --expect "calibration scpi reports transaction states and malformed commands:PASS" --summary "11 Tests 0 Failures 0 Ignored" --timeout 120
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
11 Tests 0 Failures 0 Ignored
OK
```
