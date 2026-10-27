# Project Steering Notes

## Build

This is an ESP-IDF project, not a PlatformIO project. Do not use
`platformio.exe run` from this directory because there is no `platformio.ini`.

Use the Espressif PowerShell environment profile, then run the IDF build.
To save LLM context, prefer the quiet wrapper below: it captures the verbose
ESP-IDF output to a temp log and emits only a short success line when the build
passes. If the build fails, it prints the captured log.

```powershell
$log = Join-Path $env:TEMP 'psu-ext-idf-build.log'; . C:\Espressif\tools\Microsoft.v6.1.PowerShell_profile.ps1 *> $log; idf.py build *>> $log; if (Select-String -Path $log -Pattern 'Project build complete\.' -Quiet) { 'idf.py build: OK' } else { Get-Content $log; exit 1 }
```

If you need full live build output for debugging, use the direct command:

```powershell
. C:\Espressif\tools\Microsoft.v6.1.PowerShell_profile.ps1; idf.py build
```

The generic `C:\esp\v6.1\esp-idf\export.ps1` path may fail in this setup
because the installed tools are wired through `C:\Espressif\tools`.

If the build fails with a sandbox `WinError 5` subprocess permission error,
rerun the same build command with escalated permissions. Prefer rerunning the
quiet wrapper above unless live output is needed for diagnosis.

## Preserve device calibration during tests

On-device tests must never erase or overwrite saved production calibration in
NVS (`meas_cfg`, including all calibration blobs and legacy calibration keys).
Never erase the default NVS partition, including as recovery from NVS
initialization errors. Fail the test or stop the runner instead.

Keep all test calibration reads, writes, commits, corruption fixtures, and resets
isolated from production storage for the entire executable lifetime. The unit
runner in `test/unit` links `test/support/test_nvs.c`: it redirects `meas_cfg`
opens to `test_meas_cfg` and rejects whole-partition NVS erasure. Reset only the
dedicated test namespace. Alternative runners must retain equivalent isolation.
Do not rely on backing up production calibration and restoring it after a test:
assertions, resets, and power loss can bypass cleanup.
