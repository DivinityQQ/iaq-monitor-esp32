# IAQ Monitor (ESP32-S3, ESP-IDF)

Indoor Air Quality (IAQ) monitor firmware for ESP32‑S3 built on ESP‑IDF 5.5+. Modular components, robust defaults, and a friendly console. Integrates with Home Assistant via MQTT auto‑discovery.

Current version: 0.4.0

## Features
- Wi‑Fi station mode with NVS‑stored credentials (console configurable)
- MQTT 5.0 client, retained LWT/status, HA auto‑discovery
- Central data model with explicit "no data" until sensors report
- **6 sensor drivers**: 1 real (MCU temp), 5 stubs with full simulation support
- **Simulation mode**: Complete MQTT/HA integration testing without physical sensors
- Sensor coordinator with state machine (UNINIT → INIT → WARMING → READY → ERROR)
- Per‑sensor cadences (configurable via Kconfig/console, persisted in NVS)
- Per‑sensor warm‑up periods with observable countdown
- Error recovery via console `sensor reset` commands
- Per‑sensor MQTT topics with JSON null handling for missing values
- Console commands for status, Wi‑Fi, MQTT, sensors, and cadences
- Non‑blocking MQTT publishing (enqueue)

## Hardware/Software
- Target: ESP32‑S3
- SDK: ESP‑IDF v5.5.1+
- Tooling: idf.py (CMake + Ninja)

### Supported Sensors

**Fully Supported** (real driver):
- ESP32-S3 internal temperature sensor (MCU)

**Simulated** (stub drivers, ready for hardware implementation):
- SHT41 (I2C 0x44) - Temperature & Humidity
- BMP280 (I2C 0x76) - Barometric Pressure
- SGP41 (I2C 0x59) - VOC & NOx Indices
- PMS5003 (UART) - PM1.0, PM2.5, PM10 Particulate Matter
- Senseair S8 (UART Modbus) - CO₂ Concentration

All sensors support simulation mode for testing without hardware (enable via menuconfig).

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

## Simulation Mode

Enable simulation to test the full MQTT/HA integration without physical sensors:

```bash
idf.py menuconfig
# Navigate to: Component config → Sensor Coordinator → Enable sensor simulation mode
```

When enabled:
- All sensors return realistic simulated data with jitter
- Diurnal CO₂ patterns (400-1200 ppm throughout day)
- Occasional PM spikes simulating real-world events
- Full state machine behavior (warm-up periods, error simulation)
- Complete MQTT/HA integration testing possible

Simulation is handled inside each driver, allowing seamless switching between real and simulated data.

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
- `components/sensor_drivers`: individual sensor drivers (mcu, sht41, bmp280, sgp41, pms5003, s8)
  - Bus abstraction: `i2c_bus.c` (shared), `uart_bus.c` (per-driver)
  - Simulation support: `sensor_sim.c` (conditional compilation)
- `components/sensor_coordinator`: schedules sensor reads; state machine; owns driver lifecycle
- `components/connectivity`: Wi‑Fi and MQTT; non‑blocking enqueue; HA discovery
- `components/console_commands`: shell‑style commands; interact via coordinator APIs

### Bus Ownership Architecture
- **Shared buses** (I2C for SHT41/BMP280/SGP41): Coordinator initializes once
- **Dedicated buses** (UART for PMS5003/S8): Each driver owns its port lifecycle

## Logging
- Application/component logs at INFO by default
- Chatty lower‑level Wi‑Fi tags reduced to WARN in `main.c`

## Build Notes
- If you add sources, update the component `CMakeLists.txt` `SRCS` and `REQUIRES`
- For new settings, consider Kconfig defaults and NVS persistence
- Follow CONTRIBUTING.md for coding and component guidelines

## Development Status

**Current Status (v0.4.0)**
- ✅ Core infrastructure (WiFi, MQTT, HA discovery)
- ✅ MCU temperature sensor (real driver)
- ✅ Full simulation mode for all 6 sensors
- ✅ Sensor state machine and coordinator
- ✅ Per-sensor MQTT publishing with JSON null handling
- ✅ Console commands for configuration and diagnostics
- 🚧 Hardware sensor protocols (stubs ready for implementation)
- 📋 Future: Display, LED status indicators, web configuration interface

## Troubleshooting
- On Windows, use the `\\.\COMx` device path
- If MQTT does not start: set a valid broker URL via console
- Empty SSID disables Wi‑Fi without failing init/start
- For testing without sensors: enable simulation mode in menuconfig
