# MQTT / Home Assistant Integration — Design

Status: implemented on `feature/ha-mqtt-support` (2026-10-04). Architecture:
[mqtt-architecture.md](mqtt-architecture.md).
Source intent: `psu-ext-mqtt-ha-2026-10-03.html` (R&D research, 2026-10-03).

## Goal

Native MQTT client in the firmware so PSU-EXT appears in Home Assistant via
MQTT discovery: telemetry, protection state, and CH1 output (relay) control.

## Architecture

- New component `mqtt_svc` built on ESP-IDF `esp_mqtt`.
- Disabled by default. Starts only when enabled **and** Wi-Fi is connected.
  Config changes reconnect without reboot.
- Config over SCPI, persisted in NVS namespace `mqtt_cfg` (same pattern as
  `wifi_manager` / `wifi_cfg`). There is no firmware web UI.
- TLS: `mqtt://` and `mqtts://` validated against the ESP-IDF certificate
  bundle. Custom CA upload (self-signed LAN brokers) is deferred to v2.
- Dashboard settings panel in `psu-ext-software` is a separate follow-up; it
  will be a plain client of the SCPI commands below.

### SCPI commands

| Command | Description |
|---|---|
| `SYST:MQTT:URI "<uri>"` | Broker URI; scheme must be `mqtt://` or `mqtts://` |
| `SYST:MQTT:USER "<user>"` | Username (optional) |
| `SYST:MQTT:PASS "<pass>"` | Password, write-only, never echoed |
| `SYST:MQTT:ENAB 0\|1` | Enable/disable the client |
| `SYST:MQTT:INT <ms>` | Telemetry interval, default 1000, min 200 |
| `SYST:MQTT:PREF "<prefix>"` | Discovery prefix, default `homeassistant` |
| `SYST:MQTT:STAT?` | `<state>,"<client_id>","<last_error>"`, e.g. `CONNECTED,"psu_ext_a1b2c3",""` or `ERROR,"psu_ext_a1b2c3","auth failed"` |

## Output (relay) control

- Extract the CH1 enable logic from `scpi_handler_output.c` (currently: on
  ON, `protection_svc_clear_trips()` → `protection_svc_check_ch1_enable_allowed()`
  → `output_ctrl_set_with_cause()`) into one shared function used by both SCPI
  and MQTT.
- MQTT ON behaves exactly like the UI / SCPI `OUTP CH1,1`: clears latched
  trips, re-checks input OVP, enables. This is an accepted decision.
- New cause `OUTPUT_CTRL_CHANGE_CAUSE_MQTT`.
- `relay/set`:
  - clean session, subscribe QoS 0
  - messages with the retain flag are dropped (logged)
  - only exact `ON` / `OFF` accepted; anything else ignored
- `relay/state`: retained, QoS 1, published only after `output_ctrl` confirms
  the change (listener). HA switch is not optimistic, so a refused ON snaps
  back to OFF.
- Boot: output stays off; MQTT never restores a previous state.
- Connection loss: output keeps its current state (no dead-man timeout).
  On-device OVP/OCP remain the safety net.

## Telemetry

Published every `SYST:MQTT:INT` ms to `psu_ext/<id>/measurements`, aggregated
over the window from the `measure_svc` sample listener (100 Hz):

```json
{"voltage":12.0431,"voltage_min":11.8120,"voltage_max":12.0610,
 "current":0.5240,"current_max":0.6012,"power":6.3110,"output":"ON"}
```

- `voltage`, `current`: window mean.
- `voltage_min`, `voltage_max`, `current_max`: window extremes. Purpose:
  catch bursts/droops that the mean hides (e.g. radio TX bursts vs OCP,
  brownout dips).
- `power`: mean of per-sample V·I (correct for pulsed loads).
- Values formatted from `u4` fixed point to 4 decimal places; no floats.
- No `timestamp` (HA doesn't use it; no guaranteed wall clock).
- No burst mode.
- Limitation to document: ADS1115 at 100 sps → min/max only catch events
  ≳10 ms.

## Protection and diagnostics

- Published retained on `psu_ext/<id>/protection/state`.
- Published immediately on `output_ctrl` events with a protection cause, and
  checked against the getters on each telemetry tick (publish on change).
  Needed because `protection_svc` has no change events and a CH0 OVP that
  blocks an enable does not change the output.
- Thresholds (OVP/OCP) published retained, read-only; on connect and on change.
- Last output change cause published from the `output_ctrl` listener.

## Home Assistant entities

Device-based discovery: one retained message on
`<prefix>/device/psu_ext_<id>/config` (requires HA 2024.11+).

| Entity | Type | Notes |
|---|---|---|
| Voltage | sensor | `voltage`, V, measurement |
| Voltage min | sensor | `voltage`, V |
| Voltage max | sensor | `voltage`, V |
| Current | sensor | `current`, A, measurement |
| Current max | sensor | `current`, A |
| Power | sensor | `power`, W, measurement |
| Output | switch | `relay/state` + `relay/set` |
| Input OVP | binary_sensor | `problem` |
| Output OVP | binary_sensor | `problem` |
| OCP | binary_sensor | `problem` |
| Last change cause | sensor (diagnostic) | `scpi`/`mqtt`/`trigger`/`ovp`/`ocp`/`timer` |
| OVP threshold | sensor (diagnostic) | read-only |
| OCP threshold | sensor (diagnostic) | read-only |

Explicitly out of scope for v1:
- Clear-protection button (ON already clears trips).
- Writable thresholds / OCP enable / timer / trigger entities.
- Device-side energy (Wh). Users add an HA Integral (Riemann sum) helper on
  the Power sensor; integration docs must describe this.

## Identity and topics

- `<id>` = last 3 bytes of Wi-Fi STA MAC as hex, e.g. `a1b2c3`.
- Client ID: `psu_ext_<id>`.
- Topics:

```
psu_ext/<id>/availability        online | offline (retained, LWT = offline)
psu_ext/<id>/measurements        JSON telemetry
psu_ext/<id>/relay/state         ON | OFF (retained)
psu_ext/<id>/relay/set           ON | OFF (subscribed)
psu_ext/<id>/protection/state    JSON (retained)
```

- Discovery + availability republished on every (re)connect and when HA
  publishes `online` on `<prefix>/status`.
- `sw_version` from `esp_app_get_description()`. No `configuration_url`.

## Testing and acceptance

1. **Gate — commit 1:** `OUTP` shared-function refactor plus SCPI regression
   tests proving `OUTP` behavior is unchanged. No MQTT code before this lands.
2. Pure, testable units in `mqtt_svc`, separate from `esp_mqtt` glue:
   window aggregator, `u4` formatter, telemetry/discovery JSON builders
   (cJSON), command filter, config validation.
3. Unity on-target tests for those (add `mqtt_svc` to `TEST_COMPONENTS` in
   `test/unit`), e.g. 80 mA idle + 550 mA 50 ms burst → `current_max` 0.55 A,
   mean ≈ 0.10 A.
4. `SYST:MQTT:*` tests in the `scpi_handler` suite: password never echoed,
   bad URIs and intervals rejected.
5. Manual acceptance on hardware with Mosquitto + HA 2024.11+:
   - device appears with all entities
   - output toggles both ways from HA
   - OCP trip → switch OFF, OCP sensor on, cause `ocp`
   - ON from HA clears the trip
   - retained `relay/set` is ignored
   - broker stopped → entities unavailable (LWT); recover on reconnect
   - HA restart → rediscovery
   - `mqtts://` works against a public broker

## Docs to write alongside

- SCPI setup sequence (add to `.doc/scpi-ref.md`).
- Enable auth on the broker: on a LAN broker the relay is protected only by
  broker credentials.
- Integral helper for energy.
- HA automations cannot turn the output off while the device is unavailable.
- 10 ms min/max resolution limit.

## Deviations from the intent doc

- No firmware web UI → config via SCPI.
- Protection is firmware (`protection_svc`), not hardware, and has no events.
- Device-based discovery instead of per-entity configs.
- No clear-protection button; no device-side energy sensor.
- No `timestamp`, no `configuration_url`, no burst mode.
- Straight to native firmware (Option A); Option C prototype skipped.
