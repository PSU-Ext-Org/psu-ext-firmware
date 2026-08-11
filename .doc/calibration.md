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

Each table must contain 2 through 8 points. Points must be contiguous from
`POINt1` to the current count and strictly increasing in both captured raw ADC
voltage and supplied actual value. Current calibration for `CH0` is unsupported.

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
- `POINt1` through `POINt8`, or `POIN1` through `POIN8`

| Command | Response | Notes |
|---|---|---|
| `CALibration:STARt VOLTage,CH0` | none | Opens a voltage CH0 transaction. |
| `CALibration:STARt VOLTage,CH1` | none | Opens a voltage CH1 transaction. |
| `CALibration:STARt CURRent,CH1` | none | Opens a current CH1 transaction. |
| `CALibration:TRANsaction?` | `IDLE` or `OPEN,<quantity>,<channel>` | Open responses are `OPEN,VOLTAGE,CH0`, `OPEN,VOLTAGE,CH1`, or `OPEN,CURRENT,CH1`. |
| `CALibration:ABORt` | none | Discards the open staged table. |
| `CALibration:COMMit` | none | Validates, writes NVS, publishes active RAM, and closes the transaction. |
| `CALibration:VOLTage CH0|CH1,POINt1|...|POINt8,<actual_voltage>` | none | Captures a fresh raw voltage sample and stores it with the supplied actual voltage. |
| `CALibration:VOLTage? CH0|CH1,POINt1|...|POINt8` | `<raw_voltage>,<actual_voltage>` | Uses staging for the open target; otherwise active RAM. |
| `CALibration:VOLTage:CLEar CH0|CH1` | none | Clears the matching staged voltage table. Requires a matching open transaction. |
| `CALibration:VOLTage:COUNt? CH0|CH1` | `<count>` | Uses staging for the open target; otherwise active RAM. |
| `CALibration:CURRent CH1,POINt1|...|POINt8,<actual_current>` | none | Captures a fresh raw current-sense voltage sample and stores it with supplied actual current. |
| `CALibration:CURRent? CH1,POINt1|...|POINt8` | `<raw_voltage>,<actual_current>` | Uses staging for the open target; otherwise active RAM. |
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

The firmware captures the raw ADS1115 voltage itself at command time. Query
responses return the captured raw voltage first and the supplied actual value
second:

```text
<raw_adc_voltage>,<actual_voltage_or_current>
```

## Startup and Persistence

At startup, firmware loads the three committed calibration tables from NVS once
into active RAM:

- voltage `CH0`
- voltage `CH1`
- current `CH1`

New-format records are versioned and integrity-protected. If a new-format record
is missing, the firmware attempts legacy two-point migration for that target
without writing NVS. The next successful commit writes the new blob. If both
new and legacy data are unavailable or invalid, only that target falls back to
its safe default table.

Default voltage table:

```text
raw 0.0000 V -> actual 0.0000 V
raw 1.0000 V -> actual 17.5000 V
```

Default current table:

```text
raw 0.0000 V -> actual 0.0000 A
raw 0.5000 V -> actual 1.0000 A
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
| voltage `CH0` | `0`, `1`, `2`, `3`, `5`, `8`, `15`, `25` V |
| voltage `CH1` | `0`, `1`, `2`, `3`, `5`, `8`, `15`, `25` V |
| current `CH1` | `0`, `0.1`, `0.25`, `0.5`, `0.9`, `1.3`, `1.7`, `2.4` A |

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
CALibration:VOLTage CH0,POINt2,1.0000
CALibration:VOLTage CH0,POINt3,2.0000
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
CALibration:VOLTage CH1,POINt2,1.0000
CALibration:VOLTage CH1,POINt3,2.0000
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
CALibration:CURRent CH1,POINt2,0.1000
CALibration:CURRent CH1,POINt3,0.2500
CALibration:CURRent CH1,POINt4,0.5000
CALibration:CURRent CH1,POINt5,0.9000
CALibration:CURRent CH1,POINt6,1.3000
CALibration:CURRent CH1,POINt7,1.7000
CALibration:CURRent CH1,POINt8,2.4000
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
