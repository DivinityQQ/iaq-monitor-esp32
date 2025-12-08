# Changelog

All notable changes to this project are documented in this file.

The format follows Keep a Changelog, and the project adheres to Semantic Versioning.

## [0.11.0] - 2025-12-08

OTA-focused release: dual-slot firmware/frontend OTA with rollback, new portal Update tab, and power UI polish.

Added:
- OTA manager covering firmware and frontend uploads with header checks, LittleFS remounting, rollback awareness, progress callbacks, and runtime version reporting (reads `www/version.txt`).
- HTTP/S OTA endpoints (`/api/v1/ota/info|firmware|frontend|rollback|abort`) with WebSocket `ota_progress` broadcasts; the info endpoint now surfaces firmware + frontend versions.
- Web portal "System Update" page with version/slot display, firmware + www upload cards, live progress, reboot/rollback actions, and Info dialog showing frontend version; frontend build emits `version.txt` (pkg version + git SHA) into the `www` image.

Changed:
- Flash layout switched to dual OTA slots (`ota_0`/`ota_1`) with `www` retained; bootloader rollback enabled plus new OTA validation task/Kconfig to mark images valid after Wi‑Fi/sensor/web readiness or timeout; HTTP server receive timeout raised to 30s for large uploads.
- Power dashboard form inputs refreshed (slotProps) for cleaner alignment on MUI v7.

Upgrade notes:
- Flash the updated `partitions.csv` (8MB dual-OTA layout) and rebuild the `www` image before attempting OTA updates; frontend uploads expect a LittleFS image (e.g., `www.bin`).

## [0.10.0] - 2025-12-07

Power-focused release: dedicated portal power dashboard with live controls, PowerFeather tuning, and UI/sensor polish.

Added:
- Power view in the web portal with WebSocket-fed battery/supply telemetry, rail states, charger metrics, time-left/health estimates, and alarm thresholds.
- Power control APIs plus UI for rails (EN/3V3/VSQT/STAT), charger enable/current + maintain voltage, and battery alarm limits; console power command now covers rail toggles and charger current.
- REST actions for PowerFeather: ship mode, shutdown, and power-cycle.

Changed:
- PowerFeather integration tuned: faster BQ2562x ADC locking with a 400 kHz bus, poll guard refinements, and pinout/default updates (PowerFeather pins, onboard LED GPIO46, USB console default, optional charging-on-boot).
- Display stack reorganized (split screen renderer, cached formatting) to cut redraw work; warm-up progress is now a simple countdown starting at 1s.
- Sensor coordinator refactored for clarity and consistent warm-up tracking; SGP41 conditioning confined to the first 10s window to save cycles.
- Web portal/DNS tightened: configurable static chunk size with matching HTTPD stack bump, resilient DNS parsing/startup, and state timer now also drives the power WebSocket stream.

Fixed:
- Corrected a narrowing conversion build error.
- Hardened DNS handling to reject malformed names without crashing the task.

## [0.9.0] - 2025-11-23

Major milestone: introduces runtime power management and first-class PowerFeather board support with power telemetry, controls, and portal polish.

Added:
- PowerFeather board integration using the official SDK: rail/charger/fuel-gauge telemetry surfaced via console (`power`), REST (`/api/v1/power`), WebSocket (`power` stream), and optional MQTT `/power` publishing alongside `/state`. Includes control endpoints for rails, charger, alarms, ship/shutdown, and power-cycle.
- Runtime power management and tickless mode through `pm_guard`, enabling DFS + light sleep with shared locks around sensor drivers, MQTT, and web server hotspots.

Changed:
- Migrated the I2C layer to the new ESP-IDF API and aligned the PowerFeather wrapper with project conventions. All Kconfig options are now consolidated under `main/Kconfig.projbuild` for easier navigation.
- Captive portal now lands on the dashboard, and the web portal panels/cards are standardized across dashboard/config/health views. OLED warm-up progress now animates smoothly.

Fixed:
- Corrected bad calibration reads on startup that produced invalid values.
- Resolved occasional misalignment on on-demand sensor reads, improved Senseair S8 error visibility in logs, and removed duplicate logging.

## [0.8.3] - 2025-11-20

Added:
- Stale data reporting from backend to dashboard cards to surface when sensor values stop updating.
- BMP280 hardware sleep support to reduce power/heat when idle.

Changed:
- Web portal theming overhauled with native MUI v7, unified palette, theme toggle, improved skeletons, and faster initial load (lazy `/config`, lighter data flow, package refresh).
- Expanded `www` LittleFS partition to fit the updated portal bundle.

Fixed:
- Correct Senseair S8 Modbus error parsing.
- Make BMP280 timeouts explicit and stop PMS5003 RX task before tearing down UART/queue to avoid races.

## [0.8.0] - 2025-11-04

Major release introducing the on-device Web Portal with a secure HTTP/S backend, live WebSocket updates, and a real-time single‑page dashboard. Includes a new LittleFS partition for serving static assets and a full configuration UI.

Added:
- HTTP/S web server backend with REST API (`/api/v1`) and WebSocket at `/ws`.
- SPA-based web portal (served from LittleFS `www`), including:
  - Dashboard cards for State, Metrics, and Health with live updates.
  - Charts with client-side history.
  - Configuration panel for Wi‑Fi, MQTT, and per-sensor enable/cadence.
  - Info dialog showing device, firmware and network summary.
- HTTPS support with built-in dev self‑signed cert; override via `/www/cert.pem` and `/www/key.pem`.
- Gzip static file serving and SPA fallback for client-side routing.
- Captive portal behavior in AP‑only mode (DNS redirect, HTTP redirect to `/`).
- HTTP/S server profiling hooks and expanded system profiling.

Changed:
- Switched static content storage to LittleFS; new `www` partition integrated into the build (auto‑packs `www/` if present).
- WebSocket cadence adjustments: `health` pushed at 1 Hz; improved PING/PONG handling and initial snapshot on connect.
- Optimized HTTPD task and socket handling; reduced UI redraws; layout and skeleton sizing tweaks; mobile chart label spacing; tablet portrait tuning.
- Unified format and handling of invalid sensor data across API and MQTT payloads.

Fixed:
- Resolved MQTT bad state after MQTT restart when Wi‑Fi was not connected.
- Moved Wi‑Fi RSSI acquisition out of lock to avoid contention.

Upgrade notes:
- Ensure the partition table includes the `www` LittleFS partition (see `partitions.csv`).
- If serving HTTPS with your own cert, place `cert.pem` and `key.pem` in `/www`; a helper script exists at `components/web_portal/certs/generate_cert.sh`.

## [0.7.6] - 2025-10-31

- Added: Ability to disable individual sensors.
- Changed: Tuned Kconfig defaults for better out-of-the-box behavior.
- Fixed: Ensure S8 ABC setting applies correctly on boot; renamed legacy network manager macros to MQTT manager.
- Fixed: PMS5003 error recovery logic to handle sensor faults more robustly.

## [0.7.5] - 2025-10-18

- Added: Option to disable internal pull-ups via Kconfig.
- Added: Display disconnect recovery logic and basic logging for display input.
- Changed: Reworked display to be event-driven; proper night schedule; updated console commands to match new architecture.
- Changed: Optimized default settings (performance, log level, certificate bundle).
- Changed: Default UART2 pins to free USB D-/D+ for native USB flashing.
- Changed: Improved pressure trend calculation.
- Changed: General code structure optimizations and cleanups.
- Fixed: Do not treat AQI of 0 as invalid in display.

## [0.7.2] - 2025-10-13

Improvements:
- Display driver improvements and code refinements
- SGP41 warmup logic made more intuitive
- Reworked OLED overview screen for better readability

## [0.7.1] - 2025-10-13

Features:
- Improved time sync robustness with better error handling
- Completed display driver with all 6 screens implemented
- Added button input handling with debounce and long-press detection
- Fixed compilation warnings when button is disabled

## [0.7.0] - 2025-10-13

Major Features:
- Implemented SH1106 OLED display driver (128x64 resolution)
  - 6 information screens: Overview, Environment, Air Quality, CO₂ Detail, Particulate, System
  - Button navigation support (short press = next screen, long press = toggle on/off)
  - Night mode with configurable schedule (auto-dim during specified hours)
  - Wake-on-button during night mode with configurable duration
  - Idle auto-off with configurable timeout
  - Dirty tracking for efficient screen updates
  - Console commands for display control (on/off/next/prev/screen/invert/contrast)
- Enhanced MQTT TLS support:
  - Custom Root CA PEM embedding
  - Mutual TLS (mTLS) with client certificates
  - AWS IoT Core support with ALPN for port 443
  - Flexible trust modes (certificate bundle, custom CA, or insecure for testing)
- CO₂ rate calculation improvements:
  - Switched from simple endpoint comparison to linear regression
  - Added Exponential Moving Average (EMA) smoothing to reduce jitter
  - Implemented minimum time span requirement (5 minutes) to avoid extrapolation errors
  - Added outlier clamping (±2500 ppm/hr) for more stable metrics

## [0.6.3] - 2025-10-12

Improvements:
- Tightened Wi‑Fi reconnect behavior for robustness
- Console `wifi set` now supports SSID/password with spaces (quoted)
- Added SNTP time sync component with configurable TZ and events
- Fixed CO₂ rate metric calculation

## [0.6.0] - 2025-10-12

Major Features:
- Implemented real hardware drivers: PMS5003 (PM) with background reader and smoothing, Senseair S8 (CO₂) with diagnostics/ABC controls, SHT45 (T/RH)
- Implemented SGP41 (VOC/NOx) driver using Sensirion Gas Index Algorithm (vendor code)
- Implemented BMP280 (pressure) driver with auto‑probe (0x76/0x77)
- Fusion: recompute RH at corrected temperature; used in PM humidity correction

## [0.5.5] - 2025-10-08

Fixes & Tweaks:
- Always publish score metrics; improved simulated sensor data
- Set telemetry QoS to 0 and added safeguards for full MQTT queue
- Fixed absolute humidity calculation

## [0.5.3] - 2025-10-07

Reliability:
- Optimized MQTT publish queue; clarified event naming
- Ensured metrics publishing always includes all JSON fields

## [0.5.1] - 2025-10-07

Refactoring:
- Restructured IAQ data model to separate raw and fused sensor readings
  - Added `iaq_data.raw` structure for uncompensated sensor values
  - Renamed compensated values to `iaq_data.fused` for clarity
  - Updated all components (MQTT, console, fusion, metrics) to use new structure
  - Improved data flow transparency for diagnostics and validation

## [0.5.0] - 2025-10-07

Major Features:
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

Documentation:
- Added comprehensive METRICS.md with calculation details and interpretation guidelines
- Updated architecture diagrams and data flow documentation

## [0.4.0] - 2025-10-03

- Sensor coordinator with state machine (UNINIT → INIT → WARMING → READY → ERROR)
- Per-sensor configurable cadences and warm-up periods
- Console commands for sensor control and diagnostics
- Basic MQTT integration with Home Assistant discovery
