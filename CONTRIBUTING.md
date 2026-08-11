# Contributing

Thanks for helping improve PSU-EXT Firmware. This repository contains the
ESP-IDF firmware for the PSU-EXT controller.

## License

The firmware source is licensed under the [Apache License, Version 2.0](LICENSE).
By submitting a pull request, patch, or other contribution, you confirm that
you have the right to submit it under that license.

Third-party firmware dependencies are identified in
[THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md). Do not add, copy, or modify
third-party code or dependencies without recording the applicable license,
copyright, and attribution.

## Developer Certificate of Origin (DCO)

This project uses the [Developer Certificate of Origin](https://developercertificate.org/)
(DCO), not a separate Contributor License Agreement. Every commit in a pull
request must include a `Signed-off-by` trailer, certifying that you wrote the
change or otherwise have the right to submit it.

Create signed-off commits with:

```text
git commit -s -m "Your commit message"
```

This adds a trailer such as:

```text
Signed-off-by: Your Name <your.email@example.com>
```

Use your real name and a reachable email address. To correct a missed sign-off,
use `git commit --amend -s` before opening or updating a pull request.

## Firmware development and validation

Contributors must have access to a compatible PSU-EXT device for firmware
development and validation. Test behavior that affects hardware, USB, Wi-Fi,
measurements, relay control, protection, triggers, or flashing on the device
before opening a pull request.

Use ESP-IDF 6.0. Build the firmware from the repository root after activating
the ESP-IDF environment:

```text
idf.py build
```

Component-local Unity tests are run through the `test/unit/` test project. Run
the relevant component tests for changes to `measure_svc` or `scpi_handler`.

## Before opening a pull request

- Use DCO-signed commits.
- Build the firmware with ESP-IDF 6.0.
- Test the affected behavior on a compatible PSU-EXT device.
- Run relevant component tests for measurement or SCPI changes.
- Update SCPI, calibration, README, and third-party notice documentation when
  the change affects them.
- Review the diff for generated artifacts, credentials, and unrelated changes.
