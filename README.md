# IAQ Monitor (ESP32-S3, ESP-IDF)
Indoor Air Quality (IAQ) monitor firmware for ESP32‑S3 built on ESP‑IDF 5.5+. Modular components, robust defaults, and a friendly console. Integrates with Home Assistant via MQTT auto‑discovery.
Current version: 0.6.3
## Features
- Wi‑Fi station mode with NVS‑stored credentials (console configurable)
- MQTT 5.0 client, retained LWT/status, HA auto‑discovery
- Central data model with explicit "no data" until sensors report
- **6 sensor drivers (real hardware)**: MCU temp, SHT45 (T/RH), BMP280 (pressure), SGP41 (VOC/NOx), PMS5003 (PM), Senseair S8 (CO₂)
- **Sensor fusion**: Cross-sensor compensation (PM/RH, CO₂/pressure, temp self-heating, ABC baseline)
- **Derived metrics**: EPA AQI, thermal comfort, CO₂ rate, PM spike detection, mold risk, pressure trends
- **Simulation mode**: Complete MQTT/HA integration testing without physical sensors
- **Time sync (SNTP)**: Local time via NTP with configurable TZ; event bit set when time is valid
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
**Fully Supported (real drivers):**
- ESP32‑S3 internal temperature sensor (MCU)
- SHT45 (I2C 0x44) — Temperature & Humidity
- BMP280 (I2C 0x76/0x77) — Barometric Pressure
- SGP41 (I2C 0x59) — VOC & NOx Indices (Sensirion Gas Index Algorithm)
- PMS5003 (UART) — PM1.0, PM2.5, PM10 with background reader and smoothing
- Senseair S8 (UART Modbus) — CO₂; diagnostics+ABC controls
All sensors also support simulation mode for testing without hardware (enable via menuconfig).
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
 Tip: SSID or password with spaces must be quoted, e.g.,
 ```
 wifi set "My SSID" "My Password With Spaces"
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
sensor s8 status | sensor s8 abc <on|off> [hours]
free | version | restart
```
Sensors: mcu (internal temp), sht45, bmp280, sgp41, pms5003, s8 (as drivers are wired).
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

**Publishing** (timer-based with staggered starts):
- **State**: `iaq/{device_id}/state` - Fused (compensated) readings: temp, humidity, pressure, PM1/2.5/10, CO₂, VOC/NOx indices, MCU temp, plus basic metrics (AQI value, comfort score). *Default: 30s interval*
- **Metrics**: `iaq/{device_id}/metrics` - Derived data: AQI breakdown, comfort details, pressure trend, CO₂ rate, VOC/NOx categories, mold risk, PM spike detection, overall IAQ score. *Default: 30s interval*
- **Health**: `iaq/{device_id}/health` - System diagnostics: uptime, heap, WiFi RSSI, per-sensor state/error counts/warmup status. *Default: 30s interval*
- **Diagnostics**: `iaq/{device_id}/diagnostics` - Raw (uncompensated) values + fusion parameters for validation/tuning. *Optional, default: 5min interval, enable with `CONFIG_MQTT_PUBLISH_DIAGNOSTICS=y`*
- **Status (LWT)**: `iaq/{device_id}/status` - `online`/`offline` (Last Will & Testament)

**Subscriptions** (commands):
- **Restart**: `iaq/{device_id}/cmd/restart` - Reboot device
- **Calibrate CO₂**: `iaq/{device_id}/cmd/calibrate` - Payload: `{"ppm": 415}` or `415` (defaults to 400 if empty)

**Notes:**
- Missing values publish as JSON `null` (Home Assistant shows `unknown`)
- All timers use staggered starts (0s, 5s, 10s, 15s) to flatten CPU/network load
- MQTT worker uses event coalescing: drains queue and takes single data snapshot for burst efficiency
## Home Assistant
- Home Assistant discovery payloads are published when MQTT connects (retained).
- Core entities read from `/state` (fused values) while advanced sensors use `/metrics`.
- Value templates in the discovery payload map JSON fields (for example `{{ value_json.temp_c }}`, `{{ value_json.aqi.value }}`, `{{ value_json.pressure.delta_3hr_hpa }}`).
- Optional diagnostics topic is excluded from discovery; subscribe manually when tuning sensor fusion.
## Architecture Overview
### Data Flow Pipeline
```
┌─────────────────┐
│ Sensor Drivers  │  (MCU, SHT45, BMP280, SGP41, PMS5003, S8)
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
- `components/sensor_drivers`: Individual sensor drivers (mcu, sht45, bmp280, sgp41, pms5003, s8)
  - Bus abstraction: `i2c_bus.c` (shared), `uart_bus.c` (per-driver)
  - Simulation support: `sensor_sim.c` (conditional compilation)
- `components/sensor_coordinator`: Schedules sensor reads; state machine; owns driver lifecycle; runs fusion & metrics timers
- `components/connectivity`: Wi-Fi and MQTT; non-blocking enqueue; HA discovery; queue-based worker with coalescing
- `components/console_commands`: Shell-style commands; interact via coordinator APIs
- `components/time_sync`: SNTP time sync and TZ; emits TIME_SYNCED_BIT on valid time
### Bus Ownership
- **Shared buses** (I2C for SHT4x/BMP280/SGP41): Coordinator initializes once; ESP-IDF framework provides internal locking
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
**Current Status (v0.6.3)**
- ✅ Core infrastructure (Wi‑Fi, MQTT 5.0, Home Assistant auto‑discovery)
- ✅ 6 sensor drivers with real hardware support (MCU, SHT45, BMP280, SGP41, PMS5003, S8)
- ✅ Full simulation mode for testing without hardware
- ✅ Sensor state machine with auto‑recovery (exponential backoff for ERROR states)
- ✅ Sensor fusion (PM humidity correction, CO₂ pressure/ABC compensation, temp self‑heating)
- ✅ Derived metrics (EPA AQI, thermal comfort, pressure trends, CO₂ rate, PM spike detection, mold risk)
- ✅ Unified MQTT topics (/state, /metrics, /health, /diagnostics)
- ✅ Timer‑based publishing with event coalescing and staggered starts
- ✅ Task Watchdog integration for deadlock detection
- ✅ Console commands for configuration, diagnostics, and sensor control (including S8 ABC)
- ✅ SNTP time sync with TZ support
- 📋 Future: Display, LED status indicators, web configuration interface
## Changelog

### v0.6.3 (Current)
**Improvements:**
- Tightened Wi‑Fi reconnect behavior for robustness
- Console `wifi set` now supports SSID/password with spaces (quoted)
- Added SNTP time sync component with configurable TZ and events
- Fixed CO₂ rate metric calculation

### v0.6.0
**Major Features:**
- Implemented real hardware drivers: PMS5003 (PM) with background reader and smoothing, Senseair S8 (CO₂) with diagnostics/ABC controls, SHT45 (T/RH)
- Implemented SGP41 (VOC/NOx) driver using Sensirion Gas Index Algorithm (vendor code)
- Implemented BMP280 (pressure) driver with auto‑probe (0x76/0x77)
- Fusion: recompute RH at corrected temperature; used in PM humidity correction

### v0.5.5
**Fixes & Tweaks:**
- Always publish score metrics; improved simulated sensor data
- Set telemetry QoS to 0 and added safeguards for full MQTT queue
- Fixed absolute humidity calculation

### v0.5.3
**Reliability:**
- Optimized MQTT publish queue; clarified event naming
- Ensured metrics publishing always includes all JSON fields

### v0.5.1
**Refactoring:**
- Restructured IAQ data model to separate raw and fused sensor readings
  - Added `iaq_data.raw` structure for uncompensated sensor values
  - Renamed compensated values to `iaq_data.fused` for clarity
  - Updated all components (MQTT, console, fusion, metrics) to use new structure
  - Improved data flow transparency for diagnostics and validation

### v0.5.0
**Major Features:**
- Added sensor fusion with cross-sensor compensation:
  - PM humidity correction using RH-dependent factor
  - CO₂ pressure compensation (reference 101.325 kPa)
  - CO₂ ABC (Automatic Baseline Correction) with 7-day tracking
  - Temperature self-heating offset
- Implemented derived metrics calculation (0.2 Hz timer):
  - EPA AQI (PM2.5/PM10 piecewise linear)
  - Thermal comfort (dew point, absolute humidity, heat index, comfort score/category)
  - Pressure trend (3-hour window, rising/stable/falling)
  - CO₂ rate of change (ppm/hr with median filtering)
  - PM2.5 spike detection (statistical + absolute threshold)
  - Mold risk index (dew point-based, 0-100 score)
  - VOC/NOx categorization (Excellent/Good/Moderate/Poor/Very Poor/Severe)
  - Overall IAQ score (weighted average of sub-scores)
- Enhanced MQTT architecture:
  - Migrated to unified topics (/state, /metrics, /health, /diagnostics)
  - Timer-based publishing with staggered starts (reduces CPU/network bursts)
  - Event coalescing in publish worker (single snapshot for efficiency)
  - Queue draining on disconnect (prevents stale message bursts)
- Sensor auto-recovery:
  - Exponential backoff for ERROR state sensors (30s → 60s → 120s → 300s cap)
  - Automatic reset attempts with state machine transitions
- Robustness improvements:
  - Task Watchdog Timer (TWDT) integration for deadlock detection
  - Median filtering for CO₂ rate calculation (noise suppression)
  - Enhanced error handling and logging

**Documentation:**
- Added comprehensive [METRICS.md](METRICS.md) with calculation details and interpretation guidelines
- Updated architecture diagrams and data flow documentation

### v0.4.0
- Sensor coordinator with state machine (UNINIT → INIT → WARMING → READY → ERROR)
- Per-sensor configurable cadences and warm-up periods
- Console commands for sensor control and diagnostics
- Basic MQTT integration with Home Assistant discovery

## Troubleshooting
- On Windows, use the `\\.\COMx` device path
- If MQTT does not start: set a valid broker URL via console
- Empty SSID disables Wi‑Fi without failing init/start
- For testing without sensors: enable simulation mode in menuconfig
- For wiring and default pins, see `components/sensor_drivers/GPIO_PINOUT.md`
