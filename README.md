# IAQ Monitor (ESP32-S3, ESP-IDF)

Indoor Air Quality (IAQ) monitor firmware for ESP32‑S3 built on ESP‑IDF 5.5+. Modular components, robust defaults, and a friendly console. Integrates with Home Assistant via MQTT auto‑discovery.

Current version: 0.3.1

## Features
- Wi‑Fi station mode with NVS‑stored credentials (console configurable)
- MQTT 5.0 client, retained LWT/status, HA auto‑discovery
- Central data model with explicit "no data" until sensors report
- Sensor coordinator with per‑sensor cadences (defaults via Kconfig, persisted in NVS)
- Console commands for status, Wi‑Fi, MQTT, sensors, and cadences
- Non‑blocking MQTT publishing (enqueue)

## Hardware/Software
- Target: ESP32‑S3
- SDK: ESP‑IDF v5.5.1+
- Tooling: idf.py (CMake + Ninja)

## Quick Start
```
idf.py build
idf.py -p \\.\COM3 flash
idf.py -p \\.\COM3 monitor
```
Tip: On Windows, use `\\.\COMx` or set `$env:ESPPORT='\\.\COM3'`.

## First Boot + Configuration
- Open the serial console and type `help`.
- Set Wi‑Fi credentials and reconnect:
```
wifi set <ssid> <password>
wifi restart
```
- Set MQTT broker (supports mqtt:// or mqtts://). Invalid URLs are rejected and not saved.
```
mqtt set mqtt://<host>:1883 [username] [password]
mqtt restart
```

## Console Commands (Cheat Sheet)
```
status
wifi scan | wifi set <ssid> <pass> | wifi restart | wifi status
mqtt set mqtt://<host>:1883 [user] [pass] | mqtt restart | mqtt status | mqtt publish
sensor status | sensor read <sensor> | sensor reset <sensor>
sensor calibrate co2 <ppm>
sensor cadence | sensor cadence set <sensor> <ms>
free | version | restart
```
Sensors: mcu (internal temp), sht41, bmp280, sgp41, pms5003, s8 (as drivers are wired).

## Per‑Sensor Cadence
- Defaults via Kconfig (menuconfig), persisted in NVS under `sensor_cfg`.
- Change at runtime:
```
sensor cadence
sensor cadence set <sensor> <ms>
```

## MQTT Topics
- Per‑sensor state: `iaq/{device_id}/sensor/<sensor>`
  - `mcu`: `{ "mcu_temperature": <degC|null> }`
  - `sht41`: `{ "temperature": <degC|null>, "humidity": <percent|null> }`
  - `bmp280`: `{ "pressure": <hPa|null> }`
  - `sgp41`: `{ "voc_index": <0..500|null>, "nox_index": <0..500|null> }`
  - `pms5003`: `{ "pm1_0": <ug/m3|null>, "pm2_5": <ug/m3|null>, "pm10": <ug/m3|null> }`
  - `s8`: `{ "co2": <ppm|null> }`
  - `derived`: `{ "aqi": <0..500|null>, "comfort": <string|null> }`
- Health: `iaq/{device_id}/health`
- Status (LWT): `iaq/{device_id}/status`
- Commands: `iaq/{device_id}/cmd/#`

Notes
- Each sensor publishes to its topic when it updates; no redundant aggregate payloads.
- Missing values publish as JSON `null` so HA shows "unknown".
- Home Assistant discovery config points each entity to its per‑sensor topic.

## Home Assistant
- Auto‑discovery is published on MQTT connect (retained).
- Entity mapping (topic → entity):
  - `sensor/sht41` → temperature, humidity
  - `sensor/bmp280` → pressure
  - `sensor/sgp41` → VOC index, NOx index
  - `sensor/pms5003` → PM1.0, PM2.5, PM10
  - `sensor/s8` → CO2
  - `sensor/derived` → AQI (and comfort label)
  - `sensor/mcu` → MCU temperature
- Units in HA use proper symbols: "°C" and "µg/m³". Console output uses ASCII ("degC", "ug/m3") for wide terminal compatibility.

## Architecture Overview
- `components/iaq_data`: single source of truth; mutex‑guarded; "no data" sentinels
- `components/sensor_coordinator`: schedules sensor reads; sets per‑sensor update bits; owns drivers
- `components/connectivity`: Wi‑Fi and MQTT; non‑blocking enqueue; HA discovery
- `components/console_commands`: shell‑style commands; interact via coordinator APIs

## Logging
- Application/component logs at INFO by default
- Chatty lower‑level Wi‑Fi tags reduced to WARN in `main.c`

## Build Notes
- If you add sources, update the component `CMakeLists.txt` `SRCS` and `REQUIRES`
- For new settings, consider Kconfig defaults and NVS persistence
- Follow CONTRIBUTING.md for coding and component guidelines

## Troubleshooting
- On Windows, use the `\\.\COMx` device path
- If MQTT does not start: set a valid broker URL via console
- Empty SSID disables Wi‑Fi without failing init/start
