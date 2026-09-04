# PSU-EXT Calibration SCPI Reference

This file documents the calibration SCPI commands and safe calibration
procedure. The general non-calibration SCPI reference is in
[scpi-ref.md](scpi-ref.md).

Calibration is transactional. A transaction targets exactly one table:

- voltage `CH0`: input voltage
- voltage `CH1`: output voltage
- current `CH1`: output current

Only one calibration transaction can be open device-wide. Point edits are
volatile and affect staging only. Measurements, history, OVP, and OCP continue
using the active committed tables until `CALibration:COMMit` succeeds. `ABORt`
or reset before commit discards staged changes.

Each table can contain up to 32 points and must contain at least two points for
every configured PGA. Points must be contiguous from `POINt1` to the current
count. Within each PGA, captured raw ADC voltage and supplied actual value must
both increase strictly; points from different PGAs may be interleaved. Current
calibration for `CH0` is unsupported.

AIN0, AIN1, and AIN2 autorange independently from their ADC-pin voltage. The
default ranges are +/-0.256 V, +/-0.512 V, and +/-2.048 V. The first boundary
switches upward at 0.0572 V and back downward at 0.0500 V (approximately
1.00 V and 0.875 V through a nominal 17.5:1 divider). The second switches
upward at 0.416 V and back downward at 0.384 V. The compile-time `ADS_RANGES` array in
`measure_prov_ads1115.c` accepts up to four ordered ranges selected from
+/-0.256, +/-0.512, +/-1.024, and +/-2.048 V.

## Command Reference

Accepted keyword forms are case-insensitive. Calibration commands accept either
the shown long form or the implemented short segments:

- `CAL` or `CALibration`
- `STARt` or `STAR`
- `COMMit` or `COMM`
- `ABORt` or `ABOR`
- `TRANsaction?` or `TRAN?`
- `VOLTage` or `VOLT`
- `CURRent` or `CURR`
- `CLEar` or `CLE`
- `COUNt?` or `COUN?`
- `POINt1` through `POINt32`, or `POIN1` through `POIN32`

| Command | Response | Notes |
|---|---|---|
| `CALibration:STARt VOLTage,CH0` | none | Opens a voltage CH0 transaction. |
| `CALibration:STARt VOLTage,CH1` | none | Opens a voltage CH1 transaction. |
| `CALibration:STARt CURRent,CH1` | none | Opens a current CH1 transaction. |
| `CALibration:TRANsaction?` | `IDLE` or `OPEN,<quantity>,<channel>` | Open responses are `OPEN,VOLTAGE,CH0`, `OPEN,VOLTAGE,CH1`, or `OPEN,CURRENT,CH1`. |
| `CALibration:ABORt` | none | Discards the open staged table. |
| `CALibration:COMMit` | none | Validates, writes NVS, publishes active RAM, and closes the transaction. |
| `CALibration:VOLTage CH0|CH1,POINt1|...|POINt32,<actual_voltage>` | none | Captures a fresh raw voltage sample and its active PGA, then stores it with the supplied actual voltage. |
| `CALibration:VOLTage? CH0|CH1,POINt1|...|POINt32` | `<raw_voltage>,<actual_voltage>,<pga_full_scale_voltage>` | Uses staging for the open target; otherwise active RAM. |
| `CALibration:VOLTage:CLEar CH0|CH1` | none | Clears the matching staged voltage table. Requires a matching open transaction. |
| `CALibration:VOLTage:COUNt? CH0|CH1` | `<count>` | Uses staging for the open target; otherwise active RAM. |
| `CALibration:CURRent CH1,POINt1|...|POINt32,<actual_current>` | none | Captures a fresh current-sense sample and its active PGA, then stores it with supplied actual current. |
| `CALibration:CURRent? CH1,POINt1|...|POINt32` | `<raw_voltage>,<actual_current>,<pga_full_scale_voltage>` | Uses staging for the open target; otherwise active RAM. |
| `CALibration:CURRent:CLEar CH1` | none | Clears the matching staged current table. Requires a matching open transaction. |
| `CALibration:CURRent:COUNt? CH1` | `<count>` | Uses staging for the open target; otherwise active RAM. |

Point setters may replace an existing point or append exactly `count + 1`.
Indexes that create holes are rejected. For example, after `CLEar`, `POINt3`
is rejected until points 1 and 2 exist.

If `COMMit` fails validation or NVS persistence, the transaction remains open
and the active table remains unchanged. Correct the staged points and send
`COMMit` again, or send `ABORt`.

## Value Units

Voltage arguments are volts:

```text
CALibration:VOLTage CH1,POINt4,12.0000
```

Current arguments are amps:

```text
CALibration:CURRent CH1,POINt4,0.9000
```

The firmware captures the native signed ADS1115 code and active PGA at command
time. Calibration uses that native code directly so lower PGA ranges do not
lose resolution through an intermediate 0.1 mV rounding step. Query responses
convert the stored code to ADC-pin voltage and return it first, followed by the
supplied actual value and PGA:

```text
<raw_adc_voltage_6dp>,<actual_voltage_or_current_4dp>,<pga_full_scale_voltage>
```

Raw ADC-pin voltage is signed and uses six decimal places so negative offset
codes and individual lower-range code changes remain visible. Physical
reference and measurement values continue to use the service's unsigned
four-decimal u4 representation.

The acquisition scan discards three conversions after selecting an input at
+/-0.256 V and one conversion at the other configured ranges. Calibration
capture therefore receives only a later settled conversion, which is important
with the high-impedance voltage dividers.

The service publishes each cached ADC generation once. Consequently, changing
the ADS1115 data rate changes how long an N-sample average spans, but does not
change the number of independent conversions represented by that average.

## Startup and Persistence

At startup, firmware loads the three committed calibration tables from NVS once
into active RAM:

- voltage `CH0`
- voltage `CH1`
- current `CH1`

Records are versioned and integrity-protected. The range-aware format is not
backward compatible with old fixed-range records. A missing, old, invalid, or
configured-range-incompatible record makes only that target fall back to
nominal two-point defaults for every configured PGA.

Default voltage points for the three default PGAs:

```text
PGA 0.256 V: raw 0.0000 V -> actual 0.0000 V
PGA 0.256 V: raw 0.255992 V -> actual 4.4799 V
PGA 0.512 V: raw 0.0000 V -> actual 0.0000 V
PGA 0.512 V: raw 0.511984 V -> actual 8.9597 V
PGA 2.048 V: raw 0.0000 V -> actual 0.0000 V
PGA 2.048 V: raw 2.047938 V -> actual 35.8389 V
```

Default current points for the three default PGAs:

```text
PGA 0.256 V: raw 0.0000 V -> actual 0.0000 A
PGA 0.256 V: raw 0.255992 V -> actual 0.5120 A
PGA 0.512 V: raw 0.0000 V -> actual 0.0000 A
PGA 0.512 V: raw 0.511984 V -> actual 1.0240 A
PGA 2.048 V: raw 0.0000 V -> actual 0.0000 A
PGA 2.048 V: raw 2.047938 V -> actual 4.0959 A
```

Normal readings, history, OVP, and OCP use active RAM only and do not read NVS
after startup.

## Before Calibration

Use a trusted external meter for the actual voltage/current values. Let the PSU,
load, and meter settle before each capture. Do not capture from a single
unstable conversion.

Recommended point sets:

| Target | Routine points |
|---|---|
| voltage `CH0` | `0`, `0.5`, `1.2`, `3`, `5`, `8`, `15`, `25` V |
| voltage `CH1` | `0`, `0.5`, `1.2`, `3`, `5`, `8`, `15`, `25` V |
| current `CH1` | `0`, `0.05`, `0.15`, `0.25`, `0.5`, `0.9`, `1.3`, `1.7`, `2.4` A |

Use `2.4 A` as the highest routine current calibration point so routine
calibration remains below the `2.5 A` hard current cutoff. Treat `2.5 A` as a
safety-verification point unless the fixture is designed to tolerate a trip.

## Copy-Ready Voltage CH0 Series

Set the input source to each listed voltage, wait for the reading to settle,
then send the matching command. The command text itself is ready to copy one
line at a time.

```text
CALibration:STARt VOLTage,CH0
CALibration:VOLTage:CLEar CH0
CALibration:VOLTage CH0,POINt1,0.0000
CALibration:VOLTage CH0,POINt2,0.5000
CALibration:VOLTage CH0,POINt3,1.2000
CALibration:VOLTage CH0,POINt4,3.0000
CALibration:VOLTage CH0,POINt5,5.0000
CALibration:VOLTage CH0,POINt6,8.0000
CALibration:VOLTage CH0,POINt7,15.0000
CALibration:VOLTage CH0,POINt8,25.0000
CALibration:VOLTage:COUNt? CH0
CALibration:TRANsaction?
CALibration:COMMit
CALibration:TRANsaction?
```

Expected final transaction response:

```text
IDLE
```

## Copy-Ready Voltage CH1 Series

Set the PSU output to each listed voltage, wait for the output and external
meter to settle, then send the matching command.

```text
CALibration:STARt VOLTage,CH1
CALibration:VOLTage:CLEar CH1
CALibration:VOLTage CH1,POINt1,0.0000
CALibration:VOLTage CH1,POINt2,0.5000
CALibration:VOLTage CH1,POINt3,1.2000
CALibration:VOLTage CH1,POINt4,3.0000
CALibration:VOLTage CH1,POINt5,5.0000
CALibration:VOLTage CH1,POINt6,8.0000
CALibration:VOLTage CH1,POINt7,15.0000
CALibration:VOLTage CH1,POINt8,25.0000
CALibration:VOLTage:COUNt? CH1
CALibration:TRANsaction?
CALibration:COMMit
CALibration:TRANsaction?
```

## Copy-Ready Current CH1 Series

Set the load/current to each listed value, wait for the external ammeter to
settle, then send the matching command.

```text
CALibration:STARt CURRent,CH1
CALibration:CURRent:CLEar CH1
CALibration:CURRent CH1,POINt1,0.0000
CALibration:CURRent CH1,POINt2,0.0500
CALibration:CURRent CH1,POINt3,0.1500
CALibration:CURRent CH1,POINt4,0.2500
CALibration:CURRent CH1,POINt5,0.5000
CALibration:CURRent CH1,POINt6,0.9000
CALibration:CURRent CH1,POINt7,1.3000
CALibration:CURRent CH1,POINt8,1.7000
CALibration:CURRent CH1,POINt9,2.4000
CALibration:CURRent:COUNt? CH1
CALibration:TRANsaction?
CALibration:COMMit
CALibration:TRANsaction?
```

## Abort a Transaction

This discards staged data and leaves the active committed table unchanged.

```text
CALibration:TRANsaction?
CALibration:ABORt
CALibration:TRANsaction?
```

Expected final response:

```text
IDLE
```

## Correct a Failed Commit

If `COMMit` returns an error, the transaction stays open. Query the count and
points, replace the bad point, then commit again. This example assumes an open
voltage `CH1` transaction and replaces `POINt4`.

```text
CALibration:TRANsaction?
CALibration:VOLTage:COUNt? CH1
CALibration:VOLTage? CH1,POINt4
CALibration:VOLTage CH1,POINt4,12.0000
CALibration:VOLTage? CH1,POINt4
CALibration:COMMit
CALibration:TRANsaction?
```

For current `CH1`, use the same flow with `CURRent`:

```text
CALibration:TRANsaction?
CALibration:CURRent:COUNt? CH1
CALibration:CURRent? CH1,POINt4
CALibration:CURRent CH1,POINt4,0.9000
CALibration:CURRent? CH1,POINt4
CALibration:COMMit
CALibration:TRANsaction?
```

## Post-Commit Verification

After committing a table, sweep representative values between the captured
points and compare `MEAS:VOLT?` or `MEAS:CURR?` against the external meter.
Include the endpoints and values between endpoints; calibration-point agreement
alone does not prove interpolation.

For safety verification, confirm protection behavior using calibrated readings:

```text
OVP CH0,25.0000
OVP CH1,24.0000
OCP CH1,2.4000
OCP:STATe CH1,ON
RESET:PROTect CH1
OUTP CH1,1
OVP:PROTect:STATe? CH1
OCP:PROTect:STATe? CH1
```

CH0 OVP is checked before enabling CH1 output. CH1 OVP and OCP trip the relay
off during runtime monitoring.
