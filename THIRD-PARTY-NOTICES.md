# PSU-EXT firmware: third-party notices

Copyright 2026 The PSU-EXT Authors

This document applies to the PSU-EXT firmware for ESP32-S3. The PSU-EXT
firmware source is licensed under the Apache License, Version 2.0. See the
repository's `LICENSE` file for the complete license text.

Firmware images produced from this repository incorporate third-party
software. Those components remain subject to their own license terms. This
notice must be included with source and binary firmware distributions, together
with the repository's `LICENSE` file.

## Resolved build dependencies

The versions below are taken from `dependencies.lock` and the ESP-IDF v6.0
build configuration for this repository.

| Component | Version | License | Source |
| --- | --- | --- | --- |
| ESP-IDF | 6.0.0 | Apache-2.0 | https://github.com/espressif/esp-idf |
| ESP TinyUSB | 2.1.1 | Apache-2.0 | https://components.espressif.com/components/espressif/esp_tinyusb |
| TinyUSB | 0.19.0~3 | MIT | https://github.com/hathach/tinyusb |
| FreeRTOS Kernel | included by ESP-IDF | MIT | https://github.com/FreeRTOS/FreeRTOS-Kernel |
| lwIP | included by ESP-IDF | BSD-3-Clause | https://savannah.nongnu.org/projects/lwip/ |
| Mbed TLS | included by ESP-IDF | Apache-2.0 (selected from Apache-2.0 OR GPL-2.0-or-later) | https://github.com/Mbed-TLS/mbedtls |
| FatFs | R0.15, included by ESP-IDF | FatFs license | http://elm-chan.org/fsw/ff/00index_e.html |
| HTTP Parser | 2.7.0, included by ESP-IDF | MIT | https://github.com/nodejs/http-parser |
| protobuf-c | included by ESP-IDF | BSD-2-Clause | https://github.com/protobuf-c/protobuf-c |
| SPIFFS | included by ESP-IDF | MIT | https://github.com/pellepl/spiffs |
| Newlib | included through the ESP32-S3 toolchain | mixed permissive licenses | https://sourceware.org/newlib/ |

ESP-IDF and its toolchain contain additional, target- and configuration-specific
third-party code. Their source distributions contain the authoritative license
and attribution files. A release must preserve any applicable upstream
copyright, license, and NOTICE text for the exact ESP-IDF and toolchain version
used to produce that release.

## TinyUSB (MIT License)

Copyright (c) 2018, hathach (tinyusb.org)

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

## Release-maintenance note

Update this file whenever `dependencies.lock`, the ESP-IDF version, the target,
or the enabled ESP-IDF components change. Before publishing a firmware image,
build it, then generate its release notice bundle from the exact link map:

```text
python tools/generate_release_notices.py --build-dir build --output release-notices
```

Distribute every file in `release-notices/` with the firmware image. The command
derives its inventory from the current link map and stops if an included
upstream license source is unavailable. Review the generated `MANIFEST.json`,
including its linked component list, and add notices for any new third-party
component before release.
