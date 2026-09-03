# PSU-EXT SCPI Reference

This reference covers the non-calibration SCPI commands exposed over USB CDC
and TCP port `5025`. Calibration commands and procedures are in
[calibration.md](calibration.md).

Only query commands ending in `?` return a response on success. Successful
non-query commands are accepted silently. Invalid commands, bad arguments, and
runtime failures return text lines beginning with `ERR,`.

Command keywords are case-insensitive because the parser uppercases the keyword
before dispatch. Arguments are parsed as documented per command. Numeric values
for volts, amps, and watts use decimal text and are stored internally as value
times `10000`.

## System

| Command | Response | Notes |
|---|---|---|
| `*IDN?` | `PSU-EXT,ESP32-S3,0001,0.1.0` | Device identification. |
| `SYST:ERR?` | `0,"No error"` | Also accepts `SYSTEM:ERROR?`. Recognized commands do not currently queue SCPI errors. |
| `SYST:DATETIME YYYY-MM-DD HH:MM:SS.sss` | none | Also accepts `SYSTEM:DATETIME`. Sets a runtime-only wall-clock mapping. Date must be at or after `1970-01-01 00:00:00.000`. |
| `SYST:DATETIME?` | `YYYY-MM-DD HH:MM:SS.sss` | Also accepts `SYSTEM:DATETIME?`. Returns the current mapped wall-clock time. |
| `SYST:DATETIME:MAP?` | `YYYY-MM-DD HH:MM:SS.sss,<uptime_ms>` | Also accepts `SYSTEM:DATETIME:MAP?`. Returns the base wall-clock mapping. |

The datetime mapping is not persisted. After boot, the default base mapping is
`1970-01-01 00:00:00.000,0`.

## Measurements

Supported logical channels:

| Channel | Voltage | Current | Power |
|---|---|---|---|
| `0` / `CH0` | input voltage | unsupported | unsupported |
| `1` / `CH1` | output voltage | output current | output power |

Scalar readings return decimal values with four fractional digits.

| Command | Response | Notes |
|---|---|---|
| `MEAS:VOLT? <channel>` | `<volts>` | `0`/`CH0` reads input voltage. `1`/`CH1` reads output voltage. |
| `MEAS:CURR? CH1` | `<amps>` | `1` is also accepted. `CH0` is rejected. |
| `MEAS:POWER? CH1` | `<watts>` | `1` is also accepted. `CH0` is rejected. |
| `MEAS:ADC:RATE <sps>` | none | Sets the persisted ADS1115 conversion rate. Accepted values: `8`, `16`, `32`, `64`, `128`, `250`, `475`, or `860`. `MEASURE:ADC:RATE` is also accepted. |
| `MEAS:ADC:RATE?` | `<sps>` | Returns the active ADS1115 conversion rate. `MEASURE:ADC:RATE?` is also accepted. |
| `MEAS:VOLT:AVER:COUN <count>` | none | Sets persisted scalar voltage averaging count. Values are clamped to `1..100`. |
| `MEAS:VOLT:AVER:COUN?` | `<count>` | Returns voltage averaging count. |
| `MEAS:CURR:AVER:COUN <count>` | none | Sets persisted scalar current averaging count. Values are clamped to `1..100`. |
| `MEAS:CURR:AVER:COUN?` | `<count>` | Returns current averaging count. |
| `MEAS:POWER:AVER:COUN <count>` | none | Sets persisted scalar power averaging count. Values are clamped to `1..100`. |
| `MEAS:POWER:AVER:COUN?` | `<count>` | Returns power averaging count. |

The default averaging count is `50`. A count of `1` returns the newest stored
calibrated sample. If fewer than the configured count are available, the
firmware averages all currently available samples.

The ADC rate defaults to `128` SPS and applies to all three ADS1115 inputs.
Acquisition remains single-shot and cycles through the inputs. Measurement
events continue at 100 samples/s per logical channel, so rates slower than the
publication rate produce repeated cached values. The rate cannot be changed
while a calibration transaction is open or committing.

## Measurement History

| Command | Response | Notes |
|---|---|---|
| `MEAS:VOLT:DATA? <channel>[,<count>[,<start_offset>]]` | binary block | `0`/`CH0` and `1`/`CH1` are accepted. |
| `MEAS:CURR:DATA? CH1[,<count>[,<start_offset>]]` | binary block | `1` is also accepted. |
| `MEAS:POWER:DATA? CH1[,<count>[,<start_offset>]]` | binary block | `1` is also accepted. |

History responses use SCPI definite-length binary blocks:

```text
#<digits><byte_count><records>
```

No `\r\n` is appended after a binary block. If the selected range is empty, the
response is:

```text
#10
```

Each record is 8 bytes in little-endian order:

```c
uint32_t time_ms;
uint32_t value_u4;
```

`time_ms` is device uptime in milliseconds. `value_u4` is volts, amps, or watts
multiplied by `10000`. Records are returned oldest-to-newest. `count` is
clamped to the storage capacity of `1000` samples. `start_offset` is relative to
the oldest currently buffered sample and defaults to `0`.

Example header for three records:

```text
#224<24 bytes>
```

## Output Relay

| Command | Response | Notes |
|---|---|---|
| `OUTP CH1,<0|1>` | none | `1` is also accepted for the channel. `0`/`CH0` is rejected. |
| `OUTP? CH1` | `0` or `1` | Returns relay output state. `1` is also accepted. |

`OUTP CH1,1` clears latched CH1 protection status, checks the CH0 input-voltage
pre-enable OVP condition, and drives relay GPIO `IO38` high only if the
pre-enable check passes. `OUTP CH1,0` drives the relay off. Output state is not
persisted and defaults to off after boot.

## Timer Queue

The timer queue controls delayed CH1 relay transitions. Timer IDs must be
unique 3-character strings. The queue holds up to 10 steps.

| Command | Response | Notes |
|---|---|---|
| `TIMer:ADD CH1,<id>,<seconds>,<0|1>` | none | Short form `TIM:ADD` is accepted. `<seconds>` accepts millisecond resolution such as `0.250`. |
| `TIMer:CLEar CH1` | none | Short forms `TIM:CLEAR` and `TIM:CLE` are accepted. Cancels the queue and forces relay off. |
| `TIMer:STARt CH1` | none | Short forms `TIM:START` and `TIM:STAR` are accepted. Toggles run/resume behavior. |
| `TIMer:PAUSe CH1` | none | Short forms `TIM:PAUSE` and `TIM:PAUS` are accepted. Same toggle behavior as start. |
| `TIMer:STATus? CH1` | `<id>,<status>,<seconds>` | Short forms `TIM:STATUS?` and `TIM:STAT?` are accepted. |

Timer status is `IDLE`, `RUNNING`, `PAUSED`, `OVP`, `OCP`, or `OVR`. When the
queue is empty, status returns:

```text
NONE,IDLE,0.000
```

Example:

```text
TIMer:ADD CH1,A01,1.500,1
TIMer:ADD CH1,A02,0.250,0
TIMer:STARt CH1
TIMer:STATus? CH1
```

## Trigger Configuration

Logical trigger IDs:

| Trigger | GPIO |
|---|---|
| `1` | `IO5` |
| `2` | `IO4` |

Supported trigger states are `LOW` and `HIGH`. Trigger configuration is
persisted in NVS.

| Command | Response | Notes |
|---|---|---|
| `TRIG:CONF <trigger>,<HIGH|LOW>,<function>[,<args...>]` | none | Configures or clears one trigger slot. |
| `TRIG:CONF? <trigger>,<HIGH|LOW>` | `<function>,<args...>` or `NONE` | Reads one trigger slot. |

Supported functions:

```text
OUT_ON,CH1
OUT_OFF,CH1
OUT_TOGGLE,CH1
TIM_START,CH1
TIM_PAUSE,CH1
TIM_TOGGLE,CH1
NONE
```

`NONE` clears the slot and accepts no extra arguments. In the current firmware,
`TIM_START`, `TIM_PAUSE`, and `TIM_TOGGLE` all invoke the same timer queue
toggle behavior.

Examples:

```text
TRIG:CONF 1,LOW,OUT_ON,CH1
TRIG:CONF 2,LOW,OUT_OFF,CH1
TRIG:CONF? 1,LOW
TRIG:CONF 1,HIGH,NONE
TRIG:CONF? 1,HIGH
```

Default mappings when no saved configuration exists:

```text
TRIG:CONF 1,LOW,OUT_ON,CH1
TRIG:CONF 2,LOW,OUT_OFF,CH1
```

`Trigger 1 HIGH` and `Trigger 2 HIGH` are unassigned by default.

## Protection

OVP is always enabled. OCP can be enabled or disabled for CH1. The firmware also
has a separate board-level hard current cutoff that remains active even when
user-configurable OCP is disabled.

Protection settings are persisted in NVS.

| Command | Response | Notes |
|---|---|---|
| `OVP CH0|CH1,<value>` | none | Also accepts `:SOURce:OVP`, `SOURCE:OVP`, and `SOUR:OVP`. Sets over-voltage threshold in volts. |
| `OVP? CH0|CH1` | `<volts>` | Also accepts source-prefixed forms. |
| `OCP CH1,<value>` | none | Also accepts `:SOURce:OCP`, `SOURCE:OCP`, and `SOUR:OCP`. Sets CH1 OCP threshold in amps. |
| `OCP? CH1` | `<amps>` | Also accepts source-prefixed forms. |
| `OCP:STATe CH1,<0|1|OFF|ON>` | none | Also accepts `OCP:STAT` and source-prefixed forms. |
| `OCP:STATe? CH1` | `0` or `1` | Also accepts `OCP:STAT?` and source-prefixed forms. |
| `OVP:PROTect:STATe? CH0|CH1` | `0` or `1` | Also accepts `OVP:PROT:STAT?` and source-prefixed forms. |
| `OCP:PROTect:STATe? CH1` | `0` or `1` | Also accepts `OCP:PROT:STAT?` and source-prefixed forms. |
| `RESET:PROTect CH0|CH1` | none | Also accepts `RESET:PROT` and source-prefixed forms. |

Default OVP is `25.0000 V`. The maximum accepted OVP setting is `30.0000 V`.
Default OCP is `2.5000 A`.

`CH0` OVP is used as an input-voltage pre-enable check before `OUTP CH1,1`.
`CH1` OVP trips the relay off during runtime output-voltage monitoring. CH1 OCP
trips the relay off during runtime current monitoring.

Examples:

```text
OVP CH0,25.0000
OVP CH1,24.0000
OCP CH1,2.4000
OCP:STATe CH1,ON
OVP:PROTect:STATe? CH1
OCP:PROTect:STATe? CH1
RESET:PROTect CH1
```

## WiFi

| Command | Response | Notes |
|---|---|---|
| `WIFI:SSID <ssid>` | none | Stores SSID in NVS. |
| `WIFI:SSID?` | `<ssid>` or `EMPTY` | Reads stored SSID. |
| `WIFI:PASS <password>` | none | Stores password in NVS. |
| `WIFI:PASS?` | `SET` or `EMPTY` | Does not reveal the password. |
| `WIFI:CLEAR` | none | Clears stored SSID and password. |
| `WIFI:STATUS?` | `"<ssid>",<isConnected>,<connTime>,"<ip>"` | Runtime WiFi snapshot. |

`isConnected` is `1` when connected and `0` otherwise. `connTime` is in seconds.

## Error Responses

Unknown commands return:

```text
ERR,"Unknown command"
```

Parser and runtime errors return command-specific `ERR,"..."` messages. Runtime
ESP-IDF errors are formatted as:

```text
ERR,"<context> failed: <ESP_ERR_NAME>"
```
