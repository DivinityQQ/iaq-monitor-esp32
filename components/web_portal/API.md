**Overview**
- Serves a single-page app from LittleFS at `/` (if present) and exposes a REST API under `/api/v1/*` plus a WebSocket at `/ws`.
- Protocol selection simplified for UX clarity:
  - AP-only (provisioning): HTTP only (better captive portal compatibility).
  - STA or AP+STA: HTTPS when `IAQ_WEB_PORTAL_ENABLE_HTTPS=y`.
  - Server cert/key are loaded from `/www/cert.pem` and `/www/key.pem` if present; otherwise a built-in dev self‑signed cert is used.
- JSON shapes for state/metrics/health mirror MQTT payloads produced by `mqtt_manager` via the shared `iaq_json` serializers.
- CORS enabled for API: `GET, POST, OPTIONS`, `Access-Control-Allow-Origin: <cfg>` (see `IAQ_WEB_PORTAL_CORS_ORIGIN`).
- Captive portal: in AP-only mode, DNS redirects all hostnames to the AP IP, DHCP option 114 points to `http://<ap_ip>`, and HTTP 404s redirect to `/` (non‑API URIs only).

**Base URLs**
- REST base: `/api/v1`
- WebSocket: `/ws`
- Static files: `/` (served from LittleFS partition label `www` mounted at `/www`)
 - Static assets include `Cache-Control: public, max-age=<cfg>` headers (`IAQ_WEB_PORTAL_STATIC_MAX_AGE_SEC`).

**Info**
- GET `/api/v1/info`
  - Returns device info and network summary.
  - Response: `{ device_id, version:{major,minor,patch}, uptime, free_heap, network:{ mode, wifi_connected, mqtt_connected, sta_ip?, ap_ip? } }`

**State**
- GET `/api/v1/state`
  - Fused (compensated) sensor values + basic metrics (same as MQTT `/state`).
  - Response keys (nullable until ready): `temp_c, rh_pct, pressure_hpa, pm25_ugm3, pm10_ugm3, pm1_ugm3?, co2_ppm, voc_index, nox_index, mcu_temp_c, aqi, comfort_score`.

**Metrics**
- GET `/api/v1/metrics`
  - Derived metrics (same as MQTT `/metrics`).
  - Response: `{ aqi:{ value, category, dominant, pm25_subindex, pm10_subindex }, comfort:{ score, category, dew_point_c, abs_humidity_gm3, heat_index_c }, pressure:{ trend, delta_hpa, window_hours }, co2_score, voc_category, nox_category, overall_iaq_score, mold_risk:{ score, category }, co2_rate_ppm_hr, pm25_spike_detected }`.

**Health**
- GET `/api/v1/health`
  - System health and per‑sensor runtime state.
  - Response: `{ uptime, wifi_rssi, free_heap, time_synced, epoch?, sensors:{ <sensor>:{ state, errors, last_read_s?, warmup_remaining_s? } } }`.
- GET `/api/v1/sensors`
  - Returns only `{ sensors:{ ... } }` (same content as `health.sensors`).

**Cadence**
- GET `/api/v1/sensors/cadence`
  - Returns all sensor cadences.
  - Response: `{ cadences:{ mcu|sht45|bmp280|sgp41|pms5003|s8:{ ms, from_nvs } } }`.

**Wi‑Fi**
- GET `/api/v1/wifi`
  - `{ provisioned, mode, ssid, rssi }`.
- GET `/api/v1/wifi/scan`
  - Supports optional `?limit=<5..100>` (default from config).
  - `{ aps:[ { ssid, rssi, channel, auth } ] }` or error `{ error, note? }` if not supported in current mode.
- POST `/api/v1/wifi`
  - Body: `{ ssid, password, restart? }` → saves to NVS; if `restart=true`, restarts Wi‑Fi.
  - Response: `{ status:"ok" }`.
- POST `/api/v1/wifi/restart`
  - Response: `{ status:"restarting" }`.

**MQTT**
- GET `/api/v1/mqtt`
  - `{ broker_url, configured, connected }` (password never returned).
- POST `/api/v1/mqtt`
  - Body: `{ broker_url, username?, password?, restart? }` → saves; restarts MQTT when requested (starts only if Wi‑Fi connected).
  - Response: `{ status:"ok" }`.

**Device**
- POST `/api/v1/device/restart`
  - Response: `{ status:"restarting" }`.

**Sensors (control)**
- POST `/api/v1/sensor/{id}/{action}` where `{id}` ∈ `mcu|sht45|bmp280|sgp41|pms5003|s8` and `{action}` ∈ `read|reset|enable|disable|cadence`.
  - `read`: force one read (sync, 3s timeout)
  - `reset`: reset sensor (driver re‑init)
  - `enable` / `disable`
  - `cadence`: Body `{ ms:number }` (0 disables periodic reads)
  - Response on success: `{ status:"ok" }`

**WebSocket**
- Connect to `/ws`.
- Messages are JSON envelopes: `{ type:"state"|"metrics"|"health", data:<payload> }`.
- Initial snapshot: upon connection, the server immediately pushes one `state`, `metrics`, and `health` message to that client so the UI can render without REST bootstrapping.
- Update cadence:
  - `state`: 1 Hz
  - `metrics`: every 5 s
  - `health`: 1 Hz while at least one WS client is connected
 - Heartbeats: server sends WS PINGs periodically; stale clients are removed if no PONG within timeout.
- Timers only run while at least one WS client is connected.

**Captive Portal (AP‑only)**
- DNS redirect server answers all A queries with the SoftAP IP.
- DHCP option 114 set to `http://<ap_ip>`.
- HTTP 404s redirect to `/`.
- Enabled only when running AP‑only (not AP+STA). In AP+STA the portal may be HTTPS if STA is connected.

**Conventions & Notes**
- All timestamps/durations are seconds unless noted.
- Many numeric fields can be `null` until data is ready.
- Sensor states: `UNINIT|INIT|WARMING|READY|ERROR|DISABLED`.
- Pressure units: `hPa`; PM units: `µg/m³`; CO₂ units: `ppm`.
- JSON shapes for `/state` and `/metrics` match MQTT topics; backend uses `iaq_json` for both.
- API errors are returned as JSON: `{ "error": { "code":"...", "message":"...", "status":<int> } }`.

**Quick Test (curl)**
- HTTP examples:
  - `curl http://<ip>/api/v1/info`
  - `curl http://<ip>/api/v1/state`
  - `curl http://<ip>/api/v1/metrics`
  - `curl http://<ip>/api/v1/health`
  - `curl http://<ip>/api/v1/sensors/cadence`
  - `curl -X POST http://<ip>/api/v1/wifi -H 'Content-Type: application/json' -d '{"ssid":"SSID","password":"PASS","restart":true}'`
  - `curl -X POST http://<ip>/api/v1/sensor/sht45/cadence -H 'Content-Type: application/json' -d '{"ms":5000}'`

- HTTPS with self‑signed cert:
  - When HTTPS is enabled and the device uses a self‑signed certificate (default), add `-k` (`--insecure`) to curl:
    - `curl -k https://<ip>/api/v1/info`
    - `curl -k https://<ip>/api/v1/state`
    - `curl -k -X POST https://<ip>/api/v1/wifi -H 'Content-Type: application/json' -d '{"ssid":"SSID","password":"PASS","restart":true}'`
  - Alternatively, supply the server cert/CA with `--cacert /path/to/cert.pem` instead of `-k`.
