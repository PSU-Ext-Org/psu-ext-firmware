# Home Assistant / MQTT Integration

PSU-EXT has a native MQTT client. When enabled, it registers itself in Home
Assistant through MQTT discovery and publishes telemetry, protection state, and
a switch for the CH1 output relay.

Requirements:

- An MQTT broker reachable from the PSU-EXT Wi-Fi network (for example the
  Mosquitto add-on).
- Home Assistant 2024.11 or newer with the MQTT integration configured
  (device-based discovery).

## Setup

Configure Wi-Fi first, then the broker, over USB or the TCP SCPI port:

```text
SYST:MQTT:URI "mqtt://192.168.1.10:1883"
SYST:MQTT:USER "psu"
SYST:MQTT:PASS "broker-password"
SYST:MQTT:ENAB 1
SYST:MQTT:STAT?
```

`STAT?` returns `CONNECTED,"psu_ext_<id>",""` once the client is connected. The
device then appears in Home Assistant under **Settings > Devices & services >
MQTT** as `PSU-EXT <id>`. See the [SCPI reference](scpi-ref.md#mqtt--home-assistant)
for all commands and states.

`<id>` is the last three bytes of the Wi-Fi station MAC address in lowercase
hex, for example `a1b2c3`.

## Security

On a LAN broker, the output relay is protected only by the broker's
credentials and ACLs. Anyone who can publish to `psu_ext/<id>/relay/set` can
switch the output. Enable authentication on the broker and do not allow
anonymous clients.

`mqtts://` is supported for brokers with certificates from public CAs
(validated against the ESP-IDF certificate bundle). Brokers with self-signed
certificates are not supported yet.

## Entities

| Entity | Type | Source |
|---|---|---|
| Voltage | sensor (V, measurement) | window mean |
| Voltage min | sensor (V) | window minimum |
| Voltage max | sensor (V) | window maximum |
| Current | sensor (A, measurement) | window mean |
| Current max | sensor (A) | window maximum |
| Power | sensor (W, measurement) | mean of per-sample V*I |
| Output | switch | CH1 relay |
| Input OVP | binary sensor (problem), **disabled by default** | CH0 input OVP latch |
| Output OVP | binary sensor (problem) | CH1 output OVP latch |
| OCP | binary sensor (problem) | CH1 OCP latch |
| Last change cause | diagnostic sensor | `scpi`, `mqtt`, `trigger`, `ovp`, `ocp`, `timer`, or `none` |
| OVP threshold | diagnostic sensor (V) | CH1 OVP threshold, read-only |
| OCP threshold | diagnostic sensor (A) | CH1 OCP threshold, read-only |

**Input OVP** is disabled by default because board revision 1.1.0 does not
sample the input voltage yet, so it never triggers. To show it, open the
PSU-EXT device in Home Assistant, select the Input OVP entity, and enable it.
It is set only when input overvoltage blocks a turn-on, and it stays set until
`RESET:PROTect CH0` is sent over SCPI.

The telemetry window is the `SYST:MQTT:INT` interval (default 1 s). Min and
max values show short bursts and dips that the mean hides, such as radio
transmit bursts or brownout dips.

**Resolution limit:** samples arrive at about 100 per second, so min and max
only catch events that last at least about 10 ms. Shorter spikes may be missed.

## Output switch behaviour

- Turning the switch **on** behaves exactly like `OUTP CH1,1`: it clears
  latched CH1 protection trips, re-checks the CH0 input OVP, and then enables
  the relay. If input OVP blocks the enable, the switch returns to off and the
  Input OVP sensor turns on.
- The switch state is reported only after the relay actually changes; Home
  Assistant does not assume the command succeeded.
- After a protection trip, the switch shows off, the matching OVP/OCP sensor
  shows a problem, and Last change cause shows `ovp` or `ocp`.
- The output is always off at boot. MQTT never restores a previous state.
- Retained messages on `relay/set` are ignored, so a stale retained command
  cannot switch the output on after a reconnect.
- If the connection to the broker is lost, the output keeps its current state.
  On-device OVP/OCP protection still applies.

**Home Assistant automations cannot turn the output off while the device is
unavailable.** Do not rely on an automation as the only way to switch off a
load; use the on-device OVP/OCP limits and the timer for that.

## Energy

PSU-EXT does not count energy. To track Wh, add a Home Assistant
**Integral** helper (Settings > Devices & services > Helpers > Create helper >
Integral sensor):

- Input sensor: the PSU-EXT **Power** sensor
- Integration method: **Left Riemann sum** (Trapezoidal also works)
- Metric prefix: none (Wh) or k (kWh)
- Time unit: hours

The resulting sensor can be added to the Energy dashboard as an individual
device.

## Availability

`psu_ext/<id>/availability` is `online` while connected and `offline`
otherwise. The broker publishes `offline` automatically if the device drops off
the network (last will). Disabling the client with `SYST:MQTT:ENAB 0` also
publishes `offline` first.

Discovery and availability are republished on every reconnect and whenever
Home Assistant publishes `online` on `<prefix>/status`, so entities come back
after a Home Assistant restart.

## Topics

```text
<prefix>/device/psu_ext_<id>/config  discovery (retained)
psu_ext/<id>/availability            online | offline (retained, LWT = offline)
psu_ext/<id>/measurements            telemetry JSON every interval
psu_ext/<id>/relay/state             ON | OFF (retained)
psu_ext/<id>/relay/set               ON | OFF (subscribed, QoS 0)
psu_ext/<id>/protection/state        protection JSON (retained)
```

Telemetry:

```json
{"voltage":12.0431,"voltage_min":11.8120,"voltage_max":12.0610,
 "current":0.5240,"current_max":0.6012,"power":6.3110,"output":"ON"}
```

Protection state, published on connect, on change, and checked every
telemetry interval:

```json
{"input_ovp":"OFF","output_ovp":"OFF","ocp":"ON",
 "ovp_threshold":25.0000,"ocp_threshold":2.5000,"cause":"ocp"}
```

Changing `SYST:MQTT:PREF` does not remove the retained discovery message under
the old prefix. Clear it with an empty retained publish to
`<old prefix>/device/psu_ext_<id>/config` if needed.
