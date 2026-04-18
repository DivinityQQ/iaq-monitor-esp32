# IAQ Monitor (ESP32-S3, ESP-IDF)
Indoor Air Quality (IAQ) monitor firmware for ESP32‑S3 built on ESP‑IDF 6.0+. Modular components, runtime power management, a built‑in web portal, and a friendly console. Integrates with Home Assistant via MQTT auto‑discovery. Optional PowerFeather board support adds charger/fuel‑gauge telemetry and power rail control.
Current version: 0.12.1
## Features
- Connectivity & automation: Wi‑Fi station mode with NVS‑stored credentials, captive‑portal provisioning that lands on the dashboard, MQTT 5.0 with HA auto‑discovery, HTTP/S REST API + WebSocket streaming with OTA firmware/frontend uploads, and a console for on‑device setup (plus token‑protected web console + log streaming in the portal).
- Sensors & fusion: Six real sensor drivers (MCU temp, SHT45, BMP280, SGP41, PMS5003, Senseair S8) with cross‑sensor compensation, derived metrics (AQI, comfort, CO₂ rate, PM spikes, mold risk, pressure trends), and full simulation mode for hardware‑free testing.
- Power & platform: Runtime power management (DFS + light sleep) guarded by shared PM locks; optional PowerFeather board integration via the official SDK with rail control, charger/fuel‑gauge telemetry, MQTT `/power` topic, REST/WebSocket `/power`, console `power` controls, and a portal Power dashboard for battery/rails/alarms.
- UI: SH1106 OLED with smooth warm‑up indicator, night schedule, and button navigation; on‑device SPA web portal served from LittleFS with consistent dashboard/config/health panels, history charts, notifications, and a System Update tab for OTA firmware/frontend uploads with progress + rollback.
- Security: MQTT TLS (custom CA, mutual TLS, AWS IoT ALPN) and HTTPS with built‑in or user‑provided certificates plus gzip static serving and SPA fallback.
- Reliability & observability: Central data model with explicit "no data" sentinels, per‑sensor cadences/warm‑up countdowns, staggered timers, error recovery, time sync events, watchdog integration, profiling hooks, non‑blocking MQTT publishing with queue coalescing, and automatic rollback after boot‑loops.
## Hardware/Software
- Target: ESP32‑S3 (DevKit and PowerFeather board)
- SDK: ESP‑IDF v6.0+
- Tooling: idf.py (CMake + Ninja)
- Managed components: `espressif/mqtt`, `espressif/cjson`, and `joltwallet/littlefs` via the IDF Component Manager. `dependencies.lock` pins the resolved versions; use `idf.py update-dependencies` only when you want to refresh them.
- Optional power board: PowerFeather integration via the official SDK (power rails, charger, fuel gauge, alarms). The SDK license permits use on official PowerFeather hardware.
### Supported Sensors
**Fully Supported (real drivers):**
- ESP32‑S3 internal temperature sensor (MCU)
- SHT45 (I2C 0x44) — Temperature & Humidity
- BMP280 (I2C 0x76/0x77) — Barometric Pressure
- SGP41 (I2C 0x59) — VOC & NOx Indices (Sensirion Gas Index Algorithm)
- PMS5003 (UART) — PM1.0, PM2.5, PM10 with background reader and smoothing
- Senseair S8 (UART Modbus) — CO₂; diagnostics+ABC controls
All sensors also support simulation mode for testing without hardware (enable via menuconfig).
### PowerFeather Board (optional)
- Official PowerFeather SDK integration for rail control, charger configuration, and fuel‑gauge telemetry.
- Power snapshot surfaced via console (`power`), MQTT (`/power`), REST (`/api/v1/power`), and WebSocket (`power` stream).
- Control endpoints for rails/charger/alarms/ship/shutdown when running on a PowerFeather.
- Licensed for official PowerFeather hardware only (see `components/power_board/powerfeather/LICENSE`).
## Quick Start
Linux/macOS (ESP-IDF 6.0 installed with the Espressif Installation Manager to the default path):
```bash
source ~/.espressif/v6.0/esp-idf/export.sh
idf.py build
```
If you are switching an existing checkout from ESP-IDF 5.5.x to 6.0, run `idf.py fullclean` once before the first 6.0 build to clear cached bootloader paths from the old framework checkout.

Flash/monitor:
```
idf.py -p /dev/ttyACM0 flash
idf.py -p /dev/ttyACM0 monitor
# Windows:
idf.py -p \\.\COM3 flash
idf.py -p \\.\COM3 monitor
```
The first configure/build downloads the managed components into `managed_components/` using the versions pinned in `dependencies.lock`.
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

### MQTT over TLS (MQTTS)
- Use `mqtts://<host>:8883` (or port 443 for AWS IoT with ALPN) in `mqtt set`.
- Choose trust mode in menuconfig (Component config → IAQ Monitor Configuration → MQTT TLS):
  - Certificate bundle (recommended): works with public CAs (Let's Encrypt, etc.).
  - Custom Root CA PEM: place `components/connectivity/certs/ca.pem`.
  - Insecure: disable verification (testing only).
- Mutual TLS (client certs) is supported: place `client.crt.pem` and `client.key.pem` in `components/connectivity/certs/` and enable mTLS in menuconfig.
- For AWS IoT over 443, enable AWS IoT ALPN option and use endpoint `mqtts://<your-endpoint>:443`.

## Web Portal (Dashboard + API)
- Access: open `http://<device-ip>/` (AP‑only mode) or `https://<device-ip>/` (STA or AP+STA when HTTPS is enabled). AP provisioning lands on the dashboard by default for a smoother captive‑portal flow.
- Live data: the UI connects to `/ws` for `state`, `metrics`, `health`, and `power` (PowerFeather) updates.
- API: REST endpoints under `/api/v1` (see `components/web_portal/API.md`). Quick tests:
  - `curl http://<ip>/api/v1/info`
  - `curl http://<ip>/api/v1/state`
  - `curl http://<ip>/api/v1/metrics`
  - `curl http://<ip>/api/v1/health`
  - `curl http://<ip>/api/v1/power`
  - History: `GET /api/v1/history` streams metric history data (binary `application/x-iaq-history`) for the portal charts.
- Developer console: Console tab streams `/ws/log` (device logs) and `/ws/console` (interactive shell). Auth via bearer token in the WebSocket URI query (`?token=`); one console client at a time.
- OTA updates: `/api/v1/ota/info`, POST firmware bins to `/api/v1/ota/firmware`, LittleFS images to `/api/v1/ota/frontend`, and rollback with `/api/v1/ota/rollback` (also available in the portal Update tab with live progress).
- Power controls (PowerFeather): `POST /api/v1/power/outputs`, `/power/charger`, `/power/alarms`, `/power/ship`, `/power/shutdown`, `/power/cycle`.

HTTPS & certificates
- Default: a built‑in self‑signed development certificate is used.
- Override: place `cert.pem` and `key.pem` in the LittleFS `www/` image to serve your own cert.
- Helper: use `components/web_portal/certs/generate_cert.sh` to create an ECDSA P‑256 cert with SANs for local access. Example: `./components/web_portal/certs/generate_cert.sh --embed` to write embedded defaults.
- Self‑signed testing: add `-k` to curl (e.g., `curl -k https://<ip>/api/v1/info`).

Captive portal (AP‑only)
- DNS redirects all hostnames to the SoftAP IP; HTTP 404s redirect to `/` for a smoother setup flow.

LittleFS packaging
- The build automatically packs the `www/` directory (if present) into the `www` LittleFS partition and flashes it along with the app.
- Partition table uses dual OTA slots (`ota_0`/`ota_1`) plus `www` (see `partitions.csv`); flash the updated table before using OTA.
- Tip: keep certs when syncing frontend outputs to `www/`:
  - `rsync -a --delete --exclude=cert.pem --exclude=key.pem dist/ www/`
## Console Commands (Cheat Sheet)
```
status
wifi scan | wifi set <ssid> <pass> | wifi restart | wifi status
mqtt set mqtt://<host>:1883 [user] [pass] | mqtt restart | mqtt status | mqtt publish
sensor status | sensor read <sensor> | sensor reset <sensor>
sensor calibrate co2 <ppm>
sensor cadence | sensor cadence set <sensor> <ms>
sensor s8 status | sensor s8 abc <on|off> [hours]
power
display status | display on | display off | display next | display prev
display screen <0-5> | display invert <on|off> | display contrast <0-255>
log show | log app <level> | log sys <level> | log reset
free | version | restart
```
Sensors: mcu (internal temp), sht45, bmp280, sgp41, pms5003, s8 (as drivers are wired). The `power` command reports rails/charger/fuel‑gauge data when PowerFeather support is enabled.
## OLED Display
The firmware includes an optional SH1106-based OLED display (128x64) with 6 information screens:
- **Overview**: Key metrics at a glance (CO₂, AQI, temp, humidity, pressure)
- **Environment**: Temperature, humidity, pressure with comfort indicators
- **Air Quality**: AQI breakdown, VOC/NOx indices, categories
- **CO₂ Detail**: CO₂ level, rate of change, trend
- **Particulate**: PM1.0, PM2.5, PM10, spike detection
- **System**: WiFi, MQTT status, uptime, memory, sensor states

### Configuration
Enable and configure via menuconfig:
```bash
idf.py menuconfig
# Navigate to: OLED Display (SH1106)
```
Options include:
- I2C address (default 0x3C), column offset, rotation (0°/180°)
- Contrast (0-255), refresh interval
- Idle timeout (auto-off after inactivity)
- Night schedule (start/end hours, wake duration on button press)
- Button GPIO, debounce, long-press threshold

### Button Navigation
- **Short press**: Cycle through screens (or wake from night mode)
- **Long press**: Toggle display on/off
- Console commands also available for remote control

### Features
- Dirty tracking: only redraws changed content
- Night mode: auto-dim during configured hours, wake on button press
- Idle timeout: turns off display after inactivity
- Real-time updates: sensor data, connectivity status, system info
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
- **Power** (PowerFeather only): `iaq/{device_id}/power` - Power rail + charger/fuel-gauge snapshot, gated by `CONFIG_IAQ_MQTT_PUBLISH_POWER`. Shares cadence with `/state`.
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
- Value templates in the discovery payload map JSON fields (for example `{{ value_json.temp_c }}`, `{{ value_json.aqi.value }}`, `{{ value_json.pressure.delta_hpa }}`).
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
- Runtime log level changes are available via the `log` command when dynamic log control is enabled.
## Build Notes
- If you add sources, update the component `CMakeLists.txt` `SRCS` and `REQUIRES`
- For new settings, consider Kconfig defaults and NVS persistence
- Follow CONTRIBUTING.md for coding and component guidelines
## Development Status
**Current Status (v0.12.1)**
- ✅ Core infrastructure (Wi‑Fi, MQTT 5.0, Home Assistant auto‑discovery)
- ✅ 6 sensor drivers with real hardware support (MCU, SHT45, BMP280, SGP41, PMS5003, S8)
- ✅ Full simulation mode for testing without hardware
- ✅ Sensor state machine with auto‑recovery (exponential backoff for ERROR states)
- ✅ Sensor fusion (PM humidity correction, CO₂ pressure/ABC compensation, temp self‑heating)
- ✅ Derived metrics (EPA AQI, thermal comfort, pressure trends, CO₂ rate, PM spike detection, mold risk)
- ✅ Unified MQTT topics (/state, /metrics, /health, /diagnostics)
- ✅ Timer‑based publishing with event coalescing and staggered starts
- ✅ Task Watchdog integration for deadlock detection
- ✅ Runtime power management (DFS + light sleep) with shared PM guards around I/O and compute hotspots
- ✅ Optional PowerFeather board support (rails, charger/fuel gauge telemetry, MQTT/WebSocket/REST/console `/power`, portal Power dashboard/controls)
- ✅ Console commands for configuration, diagnostics, and sensor control (including S8 ABC)
- ✅ SNTP time sync with TZ support
- ✅ OLED display (SH1106) with 6 screens, button navigation, night mode
- ✅ Enhanced MQTT TLS (custom CA, mutual TLS, AWS IoT support)
- ✅ Web Portal: HTTPS‑capable SPA with dashboard, charts, configuration (Wi‑Fi/MQTT/Sensors), and Power view
- 📋 Future: LED status indicators
## Changelog

Changelog has moved to a dedicated file: see CHANGELOG.md

## Troubleshooting
- If MQTT does not start: set a valid broker URL via console
- Empty SSID disables Wi‑Fi without failing init/start
- For testing without sensors: enable simulation mode in menuconfig
- For wiring and default pins, see `components/sensor_drivers/GPIO_PINOUT.md`
