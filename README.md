# IAQ Monitor (ESP32‑S3, ESP‑IDF)

Indoor Air Quality (IAQ) monitor firmware for ESP32‑S3 built on ESP‑IDF 5.5+. Modular components, robust defaults, and a friendly console for configuration. Designed to integrate with Home Assistant via MQTT discovery.

## Features
- Wi‑Fi station mode with NVS‑stored credentials (console configurable)
- MQTT 5.0 client, retained LWT/status, HA discovery retained
- Central data model with explicit “no data” until sensors report
- Sensor coordinator owns drivers and per‑sensor cadences (defaults via Kconfig, persisted in NVS)
- Console commands for status, Wi‑Fi, MQTT, sensor reads/resets, cadences
- Non‑blocking MQTT publish path (enqueue)

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
- Console appears on UART after boot. Press Enter to activate and type `help`.
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
- Check status at any time:
```
status
mqtt status
wifi status
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
Sensors you can reference today: mcu (internal temp). More to come (sht41, bmp280, sgp41, pms5003, s8).

## Per‑Sensor Cadence
- Build‑time defaults via Kconfig (menuconfig): Sensor Coordinator → cadence options
- Persisted at runtime in NVS (namespace `sensor_cfg`)
- View and change at runtime:
```
sensor cadence
sensor cadence set <sensor> <ms>
```

## Home Assistant
- Auto‑discovery (retained) published on MQTT connect
- State JSON publishes `null` for missing values so HA shows “unknown"�
- Topics:
  - Per-sensor state: `iaq/{device_id}/sensor/<sensor>` (e.g., `sht41`, `bmp280`, `sgp41`, `pms5003`, `s8`, `mcu`, `derived`)
  - Health: `iaq/{device_id}/health`
  - Status (LWT): `iaq/{device_id}/status`
  - Commands: `iaq/{device_id}/cmd/#`

## Logging
- App/component logs at INFO by default
- Noisy ESP Wi‑Fi/PHY tags are reduced to WARN in `main.c`
- On Windows terminals, prefer Windows Terminal (UTF‑8). Console uses ASCII units by default to avoid mojibake.

## Build Notes
- If you add new source files, update the component’s `CMakeLists.txt` `SRCS` and, if needed, `REQUIRES`
- If you add new settings, consider Kconfig defaults and NVS persistence
- See CONTRIBUTING.md for coding and component guidelines

## Troubleshooting
- Monitor warning on COM ports: use `\\.\COMx` device path
- MQTT bootloops (old behavior): now prevented; invalid broker URL disables MQTT without failing init/start
- Wi‑Fi not starting: empty SSID is treated as disabled; set via console

## Project Status / Roadmap
- Implement remaining sensor drivers and wire them into the coordinator (SHT41, BMP280, SGP41, PMS5003, S8)
- Add dependency‑aware startup (enable SGP41 after SHT41/BMP280 baselines)
- Optional: persistent MQTT session, better console arg parsing (quoted args)
- Optional: display/LED tasks (placeholders pre‑defined)


