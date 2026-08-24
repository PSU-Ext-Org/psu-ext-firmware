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

# Measurement service

The measurement service owns acquisition from the ADS1115, calibration,
sample history, configurable averaging, and CH1 power derivation. It maps
physical ADC inputs onto the logical channels used by the application and
SCPI interface.

## Public interface

Consumers should include only the focused public header they need from
`include/`:

| Header | Purpose |
| --- | --- |
| `measure_types.h` | Logical channels, physical inputs, and measurement kinds. |
| `measure_provider.h` | Provider injection and the ADS1115/fake provider contracts. |
| `measure_svc.h` | Configuration, initialization, sampling rate, and lifecycle. |
| `measure_svc_samples.h` | Events, listeners, averaging, reads, and history copies. |
| `measure_svc_calibration.h` | Transactional multi-point calibration operations. |

Headers in the component root are private contracts between implementation
modules. Application components must not depend on them.

## Data flow

Each active physical input is sampled independently at the configured rate:

```text
provider -> sampler -> active calibration -> sample event
                                      |-> history -> averaged reads
                                      |-> power pairing -> power event/history
                                      |-> calibration capture (native raw code)
                                      |-> protection listener -> CH1 relay off
```

The production application maps input voltage to ADS1115 AIN0, output voltage
to AIN1, and output current sensing to AIN2. Logical CH0 represents input
voltage when configured. Logical CH1 represents output voltage, output current,
and derived output power.

## Protection mapping

The protection service is initialized after the measurement service but before
sampling starts. It registers synchronous voltage and current listeners, so a
protection decision is made in the same event-publication path as each newly
acquired sample. Protection compares `event.value_u4`, which is the **calibrated
value**, rather than `raw_value_u4` or `raw_code`.

| Acquisition | Logical event | Protection use | Result when value is above the threshold |
| --- | --- | --- | --- |
| ADS1115 AIN1 output-voltage sample | CH1 voltage | Continuous CH1 OVP check | Latch CH1 OVP and disconnect the CH1 relay. |
| ADS1115 AIN2 output-current sample | CH1 current | Continuous configurable OCP and unconditional hard-current-limit checks | Latch CH1 OCP and disconnect the CH1 relay. |
| ADS1115 AIN0 input-voltage history | CH0 voltage | CH1 relay-enable interlock only | Latch CH0 OVP and reject the CH1 enable request. |
| Paired CH1 voltage/current | CH1 power | No protection check | Stored and published for measurement only. |

CH1 OVP and OCP use individual calibrated sample events; they do not wait for
the configurable averaging window used by `measure_svc_read()`. A trip occurs
when the sample is strictly greater than its threshold. OCP trips when either:

- the calibrated CH1 current exceeds the firmware hard-current limit, which is
  always active; or
- configurable OCP is enabled and the current exceeds its persisted OCP
  threshold.

Both cases call `output_ctrl_set_with_cause(CH1, false, ...)` from the listener
and leave a latched trip indication. Disabling configurable OCP does not disable
the hard-current limit.

CH0 input OVP follows a different path. It is not checked continuously by the
sample listener. When SCPI requests `OUTP CH1,1`, the output handler clears old
CH1 OVP/OCP trip latches and calls the protection enable interlock. That
interlock calls `measure_svc_read(CH0, VOLTAGE)`, so it uses the configured
average count over calibrated CH0 history. If CH0 is not configured, the
interlock permits enable;
if its averaged value exceeds the CH0 OVP threshold, enabling is rejected and
the CH0 OVP latch is set.

Because protection consumes calibrated values, a missing, invalid, or incorrect
calibration table changes the effective physical trip point. Calibration-table
validity and channel mapping are therefore part of the protection chain.

## Numeric domains

Normal values use unsigned **u4 fixed point**: one physical unit is stored as
10,000. For example, 2.0010 V is `20010` and 0.1250 A is `1250`.

The sampler also retains the original signed ADS1115 conversion code in each
raw event. Calibration capture averages these native codes before converting
to u4, avoiding information loss from rounding every sample independently.
Calibration interpolation and persisted tables remain in u4 for format
compatibility.

## Modules

- `measure_svc.c` is the composition root and provider/router owner.
- `measure_svc_sampler.c` schedules fresh provider conversions and publishes
  calibrated events.
- `measure_svc_event_bus.c` owns the synchronous listener registry.
- `measure_svc_history.c` stores calibrated samples in PSRAM ring buffers.
- `measure_svc_average.c` persists average-window sizes and serves averaged
  latest-value reads.
- `measure_svc_power.c` pairs CH1 voltage/current samples and derives power.
- `measure_svc_calibration.c` owns active tables and the transaction lifecycle.
- `measure_svc_calibration_capture.c` owns fresh native-code capture windows.
- `measure_svc_calibration_table.c` validates and applies piecewise-linear
  interpolation and endpoint extrapolation.
- `measure_svc_calibration_record.c` implements the stable persisted record.
- `measure_svc_calibration_persistence.c` owns NVS keys, defaults, and legacy
  two-point loading.
- `measure_prov_ads1115.c` implements production ADC acquisition.
- `measure_prov_fake.c` supplies the test/diagnostic provider.

## Calibration capture

Only one calibration transaction can be open at a time. `START` copies the
selected active table into staging. Point capture waits for events matching the
exact logical channel, kind, and physical input. Each attempt discards two
fresh samples, collects 16 signed ADC codes using a 64-bit sum, and accepts the
window when its peak-to-peak spread is at most 16 codes. An unstable attempt
starts again with a fresh discard phase. All attempts share a five-second
deadline.

The accepted signed mean is rounded once, converted once to raw u4, and stored
with the supplied reference u4 value. `COMMIT` validates the complete staging
table, persists it, and only then publishes it as active. `ABORT` discards the
staging table. Capture generation checks prevent an aborted or replaced
transaction from publishing a stale point.

Calibration commands take time to collect their fresh window. A client that
uses asynchronous command writes must wait for completion before changing the
reference source. Issuing the corresponding point query on the same ordered
connection is one way to synchronize.

## Persistence

Voltage CH0, voltage CH1, and current CH1 use independent versioned NVS blobs.
The record layout and NVS keys are compatibility-sensitive. Runtime lookup
cache fields are never persisted. Missing or invalid records fall back
independently to safe default two-point tables; recognized legacy two-point
keys are still loaded without changing the current record format.

## Tests

Unit tests live in `test/` and cover table math, record integrity, NVS loading,
transaction behavior, concurrency, and the native-code capture state machine.
The Unity device image is successful when the menu command `*` reports:

```text
16 Tests 0 Failures 0 Ignored
```

Build the production firmware from the repository root in the configured
ESP-IDF PowerShell environment with `idf.py build`.
