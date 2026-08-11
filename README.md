<h1 align="center">PSU-EXT Firmware</h1>

<p align="center">
  ESP-IDF firmware for the PSU-EXT controller.<br />
  USB CDC and Wi-Fi TCP SCPI interfaces for measurement, output control,
  protection, timers, triggers, and Wi-Fi configuration.
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

## Repository Structure

| Path | Purpose |
| --- | --- |
| `main/` | Application entry point and composition of the firmware components. |
| `components/` | Reusable ESP-IDF components for transports, SCPI handling, measurement, output control, protection, triggers, timers, and Wi-Fi. |
| `components/measure_svc/` | Measurement service and provider boundary. The active firmware build uses the ADS1115 provider; a fake provider source is retained for test and bring-up work. |
| `components/*/test/` | Component-local Unity test sources, currently for `measure_svc` and `scpi_handler`. |
| `test/unit/` | ESP-IDF Unity runner configuration and serial smoke-test support for component-local tests. |
| `managed_components/` | Dependencies resolved by the ESP-IDF Component Manager; do not edit them directly. |

## Hardware and Interfaces

| Item | Details |
| --- | --- |
| USB | USB CDC interface for SCPI commands. |
| Network | Wi-Fi station interface with a SCPI TCP server on port `5025` when connected. |
| Measurements | ADS1115-backed voltage and current measurements. |
| Output relay | CH1 relay on GPIO 38; off at boot. |
| External triggers | Trigger 1: GPIO 5; Trigger 2: GPIO 4. |
| Status LEDs | USB: GPIO 17; Wi-Fi: GPIO 18. |

## Build

Install and activate the ESP-IDF 6.0 environment for your operating system by
following Espressif's [ESP-IDF Get Started guide](https://docs.espressif.com/projects/esp-idf/en/v6.0/esp32/get-started/index.html).

From this directory, build the firmware with:

```text
idf.py build
```

## License

Copyright 2026 The PSU-EXT Authors.

The firmware source is licensed under the [Apache License, Version 2.0](LICENSE).
See [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md) for notices and license
information for third-party software included in firmware distributions.
