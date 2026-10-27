<h1 align="center">PSU-EXT Firmware</h1>

<p align="center">
  ESP-IDF firmware for the PSU-EXT controller.<br />
  USB CDC and Wi-Fi TCP SCPI interfaces for measurement, output control,
  protection, timers, triggers, and Wi-Fi configuration, plus an optional MQTT
  client for Home Assistant.
</p>

<p align="center">
  <a href="https://github.com/PSU-Ext-Org/psu-ext-firmware/actions/workflows/build.yml"><img alt="Build" src="https://github.com/PSU-Ext-Org/psu-ext-firmware/actions/workflows/build.yml/badge.svg?branch=main" /></a>
  <a href="LICENSE"><img alt="License" src="https://img.shields.io/github/license/PSU-Ext-Org/psu-ext-firmware" /></a>
</p>

## Related PSU-EXT Projects

- [PSU-EXT Root](https://github.com/PSU-Ext-Org/psu-ext)
- [Hardware](https://github.com/PSU-Ext-Org/psu-ext-hardware)
- [Control software](https://github.com/PSU-Ext-Org/psu-ext-software)

## Documentation

- [SCPI reference](.doc/scpi-ref.md)
- [Calibration guide](.doc/calibration.md)
- [Home Assistant / MQTT integration](.doc/home-assistant.md)

## Repository Structure

| Path | Purpose |
| --- | --- |
| `main/` | Application entry point and composition of the firmware components. |
| `components/` | Reusable ESP-IDF components for transports, SCPI handling, measurement, output control, protection, triggers, timers, and Wi-Fi. |
| `components/measure_svc/` | Measurement service and provider boundary. The active firmware build uses the ADS1115 provider; a fake provider source is retained for test and bring-up work. |
| `components/mqtt_svc/` | MQTT client: Home Assistant discovery, telemetry, protection state, and CH1 output control. |
| `components/*/test/` | Component-local Unity test sources, currently for `measure_svc`, `mqtt_svc`, and `scpi_handler`. |
| `test/unit/` | ESP-IDF Unity runner configuration and serial smoke-test support for component-local tests. |
| `managed_components/` | Dependencies resolved by the ESP-IDF Component Manager; do not edit them directly. |

## Hardware and Interfaces

| Item | Details |
| --- | --- |
| USB | USB CDC interface for SCPI commands. |
| Network | Wi-Fi station interface with a SCPI TCP server on port `5025` when connected. |
| MQTT | Optional client with Home Assistant MQTT discovery; configured over SCPI, disabled by default. |
| Measurements | ADS1115-backed voltage and current measurements. |
| Output relay | CH1 relay on GPIO 38; off at boot. |
| External triggers | Trigger 1: GPIO 5; Trigger 2: GPIO 4. |
| Status LEDs | USB: GPIO 17; Wi-Fi: GPIO 18. |

## Build

Install and activate the ESP-IDF 6.1 environment for your operating system by
following Espressif's [ESP-IDF Get Started guide](https://docs.espressif.com/projects/esp-idf/en/v6.1/esp32s3/get-started/index.html).

From this directory, build the firmware with:

```text
idf.py build
```

`sdkconfig.defaults` contains the reviewed board and application settings.
ESP-IDF generates the ignored `sdkconfig` locally; `sdkconfig.old` is ignored too.
An existing `sdkconfig` takes precedence over defaults. To adopt changed defaults,
back up any local overrides, remove `sdkconfig`, and run `idf.py reconfigure`.
Use `idf.py menuconfig` for local changes and `idf.py save-defconfig` to extract
overrides for review before updating the tracked defaults.

The defaults select ESP32-S3, 16 MB flash in DIO mode at 80 MHz, the large
single-app partition, and the external RTC crystal. Octal PSRAM runs at 80 MHz
with boot-time capacity detection, memory testing, and ECC. On the 16 MB PSRAM
device, ECC reserves 1 MB; PSRAM allocations use the explicit capabilities API.
USB CDC uses DMA and 512-byte RX, TX, and endpoint buffers. Wi-Fi is enabled
with ESP-IDF's default network settings. The existing calibration policy is
preserved. Application logging defaults to INFO, with DEBUG and VERBOSE disabled.
The interrupt watchdog retains its existing 300 ms timeout.

The separate `test/unit/sdkconfig.defaults` configures the Unity runner and its
USB Serial/JTAG console. Its generated configuration is also ignored.

## Relative voltage and current readings

`MEAS:VOLT:REL <channel>,<volts>` sets a REL offset for `CH0` or `CH1`.
`MEAS:CURR:REL CH1,<amps>` sets the current offset. Numeric channel aliases
`0` and `1` are accepted. Query the offset with `MEAS:VOLT:REL? <channel>`
or `MEAS:CURR:REL? CH1`; responses have four decimal places.

For example, `MEAS:CURR:REL CH1, 0.0002;` subtracts 0.0002 A from the
averaged scalar current reading. `MEAS:CURR:REL CH1, 0` restores absolute
readings. These REL commands accept an optional trailing semicolon, without
command chaining. Offsets are nonnegative, rounded to four decimal places,
and held only in RAM; reboot resets them to zero.

REL affects scalar voltage/current SCPI queries and OLED voltage/current
readings. Results below zero are clamped to zero. DATA queries and stored
sample history, MQTT, calibration, and derived power remain absolute.

A successful calibration transaction start clears all three REL offsets.
Nonzero offsets are rejected while the transaction is OPEN or COMMITTING.
Offsets remain zero after commit or abort; failed starts leave them unchanged.

During calibration, **all OCP/OVP checks are bypassed**, including the hard
current cutoff and the CH0 voltage check when enabling CH1. Protection settings,
thresholds, existing trip flags, and relay state are not changed by starting
calibration. Explicit output commands and timers continue to work. Protection
resumes after successful commit or abort; a failed commit leaves the transaction
open and protection bypassed. Resumed sample checks use absolute readings.

## License

Copyright 2026 The PSU-EXT Authors.

The firmware source is licensed under the [Apache License, Version 2.0](LICENSE).
See [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md) for notices and license
information for third-party software included in firmware distributions.
