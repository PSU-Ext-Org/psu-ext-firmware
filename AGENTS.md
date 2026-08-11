# Project Steering Notes

## Build

This is an ESP-IDF project, not a PlatformIO project. Do not use
`platformio.exe run` from this directory because there is no `platformio.ini`.

Use the Espressif PowerShell environment profile, then run the IDF build.
To save LLM context, prefer the quiet wrapper below: it captures the verbose
ESP-IDF output to a temp log and emits only a short success line when the build
passes. If the build fails, it prints the captured log.

```powershell
$log = Join-Path $env:TEMP 'psu-ext-idf-build.log'; . C:\Espressif\tools\Microsoft.v6.0.PowerShell_profile.ps1 *> $log; idf.py build *>> $log; if (Select-String -Path $log -Pattern 'Project build complete\.' -Quiet) { 'idf.py build: OK' } else { Get-Content $log; exit 1 }
```

If you need full live build output for debugging, use the direct command:

```powershell
. C:\Espressif\tools\Microsoft.v6.0.PowerShell_profile.ps1; idf.py build
```

The generic `C:\esp\v6.0\esp-idf\export.ps1` path may fail in this setup
because the installed tools are wired through `C:\Espressif\tools`.

If the build fails with a sandbox `WinError 5` subprocess permission error,
rerun the same build command with escalated permissions. Prefer rerunning the
quiet wrapper above unless live output is needed for diagnosis.
