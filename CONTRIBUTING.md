# Contributing Guidelines

Thanks for your interest in contributing to the IAQ Monitor firmware. This document explains the project structure, coding patterns, and the workflow we follow.

## Project Structure
- ESP‑IDF components under `components/` with their own `CMakeLists.txt`
- Main application under `main/`
- Shared data model in `components/iaq_data`
- Connectivity (Wi‑Fi/MQTT) in `components/connectivity`
- Sensor drivers in `components/sensor_drivers`
- Sensor coordinator in `components/sensor_coordinator`
- Console commands in `components/console_commands`

## Development Workflow
1. Create a feature branch from `main`.
2. Make focused changes in the appropriate component.
3. Update/verify the component's `CMakeLists.txt` `SRCS` and `REQUIRES` if you add sources or new dependencies.
4. Build and test locally with `idf.py build flash monitor`.
5. Submit a PR with a clear description and rationale.

## Coding Conventions
- C (C99) for firmware
- Keep modules cohesive; avoid leaking responsibilities across components.
- Public headers in `include/`; private headers in the component root (or `PRIV_INCLUDE_DIRS`).
- Prefer static linkage within components; minimize global symbols.
- Logging:
  - Use component TAGs (e.g., `static const char *TAG = "SENSOR_COORD";`).
  - INFO for high‑level milestones, WARN/ERROR for problems, DEBUG for detail.
  - Avoid excessive logs in tight loops.

## iaq_data (Thread Safety)
- `iaq_data` is the single source of truth for runtime state.
- Use `IAQ_DATA_WITH_LOCK()` when accessing fields.
- Never call functions that also lock `iaq_data` while holding the lock (mutex is not recursive).
- “No data” conventions:
  - Floating‑point values set to `NAN` until first valid reading.
  - Integer indices (VOC/NOx/AQI) use `UINT16_MAX` as sentinel.
  - Console prints `n/a`; MQTT publishes JSON `null` for missing values.
  - Per-sensor timestamps (`updated_at`) are seconds since boot; `0` means "never". Not exposed via MQTT; console prints ages.

## Sensor Drivers & Coordinator
- Drivers live in `components/sensor_drivers` and should expose a narrow API: `init/enable/disable/read` (+ optional `reset`/`calibrate`).
- The sensor coordinator owns driver lifecycle and scheduling.
- Do not call drivers from the console; instead, add coordinator APIs that enqueue commands to be executed in the coordinator task.
- Per‑sensor cadence:
  - Defaults come from Kconfig.
  - Persist changes to NVS (`sensor_cfg` namespace) via `sensor_coordinator_set_cadence()`.
  - Provide `get_cadences()` for observability.
  - On successful read, set the corresponding `SENSOR_UPDATED_*_BIT` so the network monitor can publish that sensor's payload.

## Connectivity (Wi‑Fi/MQTT)
- Wi‑Fi creds in NVS (`wifi_config/{ssid,password}`). Empty SSID disables Wi‑Fi without failure.
- MQTT config in NVS (`mqtt_config/{broker_url,username,password}`). Reject invalid URLs (must include `mqtt://` or `mqtts://`).
- MQTT operations should be non‑blocking; use `esp_mqtt_client_enqueue()`.
- Home Assistant discovery payloads should be retained.
 - State is published per-sensor to `iaq/<device_id>/sensor/<sensor>` when that sensor updates; avoid redundant publishes.

## Console Commands
- Keep commands simple and fail‑safe; return 0 for usage (not an error).
- Avoid driver headers in console; depend on coordinator/public APIs.
- For new features, include a `status`/`list` command to show current config and provenance (e.g., NVS vs default).

## Adding a New Sensor
1. Create `<sensor>_driver.h/.c` in `components/sensor_drivers`.
2. Expose APIs: `init/enable/disable/read` (and `reset`/`calibrate` if applicable).
3. Add driver to `sensor_drivers/CMakeLists.txt` `SRCS` and `REQUIRES`.
4. Wire scheduling into `sensor_coordinator` with cadence + startup gating if needed.
5. Extend console commands (read/reset/calibrate) to call coordinator APIs.
6. Update MQTT state and HA discovery as appropriate.

## NVS & Kconfig
- Kconfig (`Kconfig.projbuild`) for defaults users can set in menuconfig.
- Persist runtime changes to NVS; load at boot, writing defaults if keys are missing.
- Use clear namespaces and keys (see existing `wifi_config`, `mqtt_config`, `sensor_cfg`).

## PR Checklist
- [ ] Component boundaries respected (no cross‑layer leakage)
- [ ] `CMakeLists.txt` updated for new sources/deps
- [ ] `iaq_data` fields initialized and documented
- [ ] Console usage/help updated (if commands changed)
- [ ] MQTT payloads avoid bogus zeros (use `null` for missing)
- [ ] Logs at appropriate levels; no noisy loops
- [ ] Build + flash + smoke test passed

## Contact / Help
- If something’s unclear, open a draft PR with your approach and questions.
- Prefer small, incremental changes over large refactors.
