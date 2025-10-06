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
- State: `iaq/{device_id}/state` - fused readings (temperature, humidity, pressure_hpa, PM, CO2, AQI, comfort score).
- Metrics: `iaq/{device_id}/metrics` - derived data (AQI breakdown, comfort details, pressure trend, CO2 rate, VOC/NOx categories, mold risk, PM spike flag, overall scores).
- Health: `iaq/{device_id}/health` - uptime, heap, Wi-Fi RSSI, per-sensor state/error counters.
- Diagnostics (optional): `iaq/{device_id}/diagnostics` - raw values and fusion diagnostics when `CONFIG_MQTT_PUBLISH_DIAGNOSTICS=y`.
- Status (LWT): `iaq/{device_id}/status`
- Commands: `iaq/{device_id}/cmd/#` (`/cmd/restart`, `/cmd/calibrate`)
Notes
- Missing values publish as JSON `null` so Home Assistant shows `unknown`.
- State/metrics timers default to 30 s (configurable via Kconfig); diagnostics defaults to 5 min.
- Calibration command accepts `{"ppm": 415}` or plain integers; defaults to 400 ppm when the payload is empty.
## Home Assistant
- Home Assistant discovery payloads are published when MQTT connects (retained).
- Core entities read from `/state` (fused values) while advanced sensors use `/metrics`.
- Value templates in the discovery payload map JSON fields (for example `{{ value_json.temp_c }}`, `{{ value_json.aqi.value }}`, `{{ value_json.pressure.delta_3hr_hpa }}`).
- Optional diagnostics topic is excluded from discovery; subscribe manually when tuning sensor fusion.
## Architecture Overview
### Data Flow Pipeline
```
┌─────────────────┐
│ Sensor Drivers  │  (MCU, SHT41, BMP280, SGP41, PMS5003, S8)
└────────┬────────┘
         │ Raw readings (5-10s cadence, staggered)
         ↓
┌─────────────────┐
│ Sensor Fusion   │  (1 Hz timer)
│   (fusion.c)    │  • Temp self-heating correction
└────────┬────────┘  • PM humidity compensation
         │           • CO₂ pressure + ABC baseline
         ↓
┌─────────────────┐  Fused values → iaq_data.fused
│  iaq_data_t     │  (mutex-guarded global state)
│ (single source) │
└────────┬────────┘
         │
         ↓
┌─────────────────┐
│ Metrics Calc    │  (0.2 Hz / 5s timer)
│ (metrics_calc.c)│  • AQI (EPA), comfort, trends
└────────┬────────┘  • CO₂ rate (median-filtered)
         │           • PM spike detection
         ↓           • Pressure trend, mold risk
┌─────────────────┐  Metrics → iaq_data.metrics
│  MQTT Worker    │
│ (queue-based)   │  Timers fire → events enqueued → coalesced
└────────┬────────┘  • Single snapshot per batch
         │           • Publishes /state, /metrics, /health
         ↓
   Home Assistant
```
**Key Features:**
- **Staggered reads:** Sensors start at offset intervals to flatten I²C/UART load
- **Decoupled processing:** Fusion (1 Hz) and metrics (0.2 Hz) run independently
- **Event coalescing:** MQTT worker drains queue and takes single snapshot for burst publishes
- **Auto-recovery:** ERROR-state sensors retry with exponential backoff (30s → 5min cap)
- **Watchdog monitoring:** Coordinator and MQTT tasks feed TWDT to detect deadlocks
### Component Responsibilities
- `components/iaq_data`: Single source of truth; mutex-guarded; "no data" sentinels
- `components/sensor_drivers`: Individual sensor drivers (mcu, sht41, bmp280, sgp41, pms5003, s8)
  - Bus abstraction: `i2c_bus.c` (shared), `uart_bus.c` (per-driver)
  - Simulation support: `sensor_sim.c` (conditional compilation)
- `components/sensor_coordinator`: Schedules sensor reads; state machine; owns driver lifecycle; runs fusion & metrics timers
- `components/connectivity`: Wi-Fi and MQTT; non-blocking enqueue; HA discovery; queue-based worker with coalescing
- `components/console_commands`: Shell-style commands; interact via coordinator APIs
### Bus Ownership
- **Shared buses** (I2C for SHT41/BMP280/SGP41): Coordinator initializes once; ESP-IDF framework provides internal locking
- **Dedicated buses** (UART for PMS5003/S8): Each driver owns its port lifecycle
**For detailed metrics documentation, see [METRICS.md](METRICS.md).**
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