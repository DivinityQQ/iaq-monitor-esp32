# PowerFeather SDK Integration Plan

This document outlines how to integrate the PowerFeather SDK (v1.1.0) into this IAQ Monitor firmware, expose the board’s power features through console and web portal, and keep the project maintainable. The goal is to be ready to flash on an ESP32‑S3 PowerFeather and immediately use battery‑aware features (independent 3V3 rail control, VSQT rail, charger/fuel‑gauge, ship/shutdown modes, MPP voltage, alarms, and telemetry).

## Footprint and Licensing
- Size: ~3.1 kLoC, ~392 KB repo; main files: `Mainboard.cpp/h`, `BQ2562x.cpp/h`, `LC709204F.cpp/h`, `Utils` (I2C/Mutex/Result).
- License: PowerFeather 4‑Clause (allowed on official PowerFeather hardware only). Document this in release notes.
- Language: C++17 (uses C++ classes + ESP-IDF APIs).

## Dependency (Managed Components)
Add the SDK via ESP-IDF component manager so it lives under `managed_components` (declare in `components/power_board/idf_component.yml`, not in web_portal):
```
idf.py add-dependency "powerfeather/powerfeather-sdk^1.1.0"
```
This updates `dependencies.lock`. Ensure we commit the lockfile. If we prefer a manifest pin, add to `components/power_board/idf_component.yml` (or create a top-level `idf_component.yml`) under `dependencies`:
```yaml
dependencies:
  powerfeather/powerfeather-sdk: "^1.1.0"
```

## Build/Toolchain Notes
- Enable C++ in the consuming component: set `CXX` sources in the new wrapper component and make sure `idf_component_register` lists `.cpp` sources.
- The SDK expects ESP-IDF 4.4+; our project uses 5.5.x, which is compatible.
- The SDK uses I2C port 1 on pins SDA1=47, SCL1=48 (per `Mainboard::Pin`). Our existing sensors use I2C0; no pin conflict.
- SDK depends on `driver`, `esp_timer`, `nvs_flash`, `spi_flash` (per its CMake).

## Hardware Features to Expose
- Rails and pins: `EN` (FeatherWing enable), `EN_3V3` (header 3V3), `EN_SQT` (VSQT STEMMA QT rail), `STAT` LED gate, `PG`/`ALARM` status pins.
- Power controls: enable/disable 3V3, VSQT, EN; maintain voltage (MPP) for solar; ship mode; shutdown mode; power-cycle.
- Charger: enable/disable charging; set max charge current; set maintain voltage.
- Fuel gauge (LC709204F): battery voltage, current, charge %, health %, cycles, time remaining, temp; alarms for low/high voltage and low charge.
- Measurements: supply voltage/current/good flag.

## Integration Layers
1. **Wrapper Component (`components/power_board`)**
   - Add `power_board.cpp/.h` with C APIs wrapping `PowerFeather::Board`. Convert `Result`→`esp_err_t`.
   - Provide init: `power_board_init(capacity_mAh, BatteryType)`; expose enums or simple ints for battery type.
   - Expose APIs: set/get EN, 3V3, VSQT; enable STAT; supply readings; maintain voltage; ship/shutdown/power-cycle; charger enable/limit; fuel-gauge enable; battery voltage/current/charge/health/cycles/time_left/temp; alarms (low/high voltage, low charge); update fuel-gauge temp.
   - Use `pm_guard` for bus/no-sleep locks around blocking I2C calls.
   - Make feature optional via Kconfig (e.g., `CONFIG_IAQ_POWERFEATHER_ENABLE`) with battery defaults (capacity/type) and a “fail-soft” flag to skip errors on non-PF boards.

2. **Initialization (main)**
   - After NVS/event loop but before sensors: call `power_board_init(...)`.
   - If disabled or not PF hardware: log and continue without failing boot.

3. **Data Model (`iaq_data`)**
   - Add `power` struct: `supply_mv`, `supply_ma`, `supply_good`, `maintain_mv`, `en`, `v3v_on`, `vsqt_on`, `charging_on`, `charge_limit_ma`, `batt_mv`, `batt_ma`, `charge_pct`, `health_pct`, `cycles`, `time_left_min`, `batt_temp_c`, `alarms` (low_v_mv, high_v_mv, low_pct), `updated_at`.
   - Protect with existing mutex and mark validity flags.

4. **API Serialization (`iaq_json`)**
   - Build a dedicated `/power` JSON payload (do **not** extend `/health`): mirror the `power` struct; include nulls when unavailable.
   - WebSocket: send separate `type:"power"` messages; keep existing `health` unchanged.

5. **Web Portal Backend (`web_portal.c`)**
   - REST:
     - `GET /api/v1/power` → power snapshot (optional; guard with PF enable).
     - `POST /api/v1/power/outputs` (body: `en`, `v3v_on`, `vsqt_on`, `stat_on`).
     - `POST /api/v1/power/charger` (body: `enable`, `limit_ma?`, `maintain_mv?`).
     - `POST /api/v1/power/alarms` (body: `low_v_mv?`, `high_v_mv?`, `low_pct?`).
     - `POST /api/v1/power/ship|shutdown|cycle` (actions warn about session drop).
   - Validation: clamp ranges to SDK limits (e.g., voltage 2500–16800 mV, charge current up to 2000 mA, percent 1–100).
   - Map SDK errors to HTTP errors with messages.

6. **Frontend**
   - Dashboard “Power” card: battery gauge %, voltage/current, time-left, charge/health, supply good/bad, maintain voltage, charger enable/limit, rail toggles (3V3, VSQT, EN).
   - Alarms: inputs for low/high voltage and low charge; show alarm states.
   - Live updates from WS.

7. **Console Commands**
   - Add `power` command with subcommands:
     - `status`
     - `outputs en|3v3|vsqt on|off`
     - `charger on|off [limit_ma]`
     - `maintain <mv>`
     - `battery read|alarm lowv|highv|lowpct <val>`
     - `ship|shutdown|cycle`
   - Return nonzero on SDK failures; pretty-print `Result`.

8. **MQTT (optional)**
   - Publish power block under a dedicated topic (e.g., `/power`); keep `/health` as-is.
   - Add retained alarm config topic if needed.

## Configuration Defaults
- Kconfig entries:
  - Enable PF support (bool).
  - Battery capacity (mAh) and type (enum Generic/ICR18650_26H/UR18650ZY).
  - Default maintain voltage (mV) for solar use (0 to disable).
  - Default charge current limit (mA).
  - “Fail-soft” on non-PF hardware (skip init errors).
  - MQTT publish toggle for `/power` (default off).
- Note: EN/3V3/VSQT start states are set by SDK init; document them in help text.

## Safety/UX Considerations
- Ship/shutdown/power-cycle will drop console/Wi‑Fi; require explicit confirmation in console (e.g., `power ship confirm`).
- Clamp inputs to SDK limits; return helpful errors.
- If VSQT is off, many calls return InvalidState; handle gracefully.
- Keep I2C0 sensors separate from PF I2C1 bus.

## Testing Checklist
- Build/link with SDK present; build still succeeds when PF support disabled.
- Init path on non-PF hardware: logs warning, continues.
- Functional on hardware:
  - Toggle EN/3V3/VSQT and observe rails.
  - Read supply voltage/current and battery metrics with/without USB.
  - Charger enable/disable and current limit changes.
  - Maintain voltage set and honored (solar MPP).
  - Alarms fire when thresholds crossed.
  - Ship/shutdown/power-cycle behave as expected.
- API/WS/console/UI reflect changes and errors.

## Next Steps (status)
- [x] Move plan into `components/power_board`.
- [x] Add dependency manifest, Kconfig, and wrapper component.
- [x] Integrate init into `main.c` (fail-soft).
- [x] Add `/power` JSON builder, REST endpoint, WS broadcast, console `power` status.
- [x] Add MQTT publish toggle and topic.
- [x] Extend `iaq_data` for power (cached snapshot) and poll SDK into it.
- [x] Increase HTTP URI handler slots to accommodate new endpoints.
- [ ] Add frontend card and fuller REST controls (alarms/actions) UI.
- [ ] Test on hardware (USB, battery, solar if available).
