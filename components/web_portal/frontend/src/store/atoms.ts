import { atom } from 'jotai';
import { atomFamily, atomWithStorage, selectAtom } from 'jotai/utils';
import type { State, Metrics, Health, Power, DeviceInfo, SensorId, SensorCadence, MQTTStatus, OTAProgress, OTAVersionInfo } from '../api/types';
// Color derivations moved to components using theme CSS variables for live updates
import { apiClient } from '../api/client';

// ============================================================================
// PRIMITIVE ATOMS - Raw Data Storage
// ============================================================================

/**
 * WebSocket connection state atoms
 */
export const wsConnectedAtom = atom<boolean>(false);
export const wsReconnectingAtom = atom<boolean>(false);

/**
 * Real-time data atoms (updated via WebSocket)
 * - State: 1Hz updates (sensor readings)
 * - Metrics: 5s updates (computed metrics like AQI, comfort)
 * - Health: 1Hz updates while WS clients connected (system health, uptime, sensors)
 */
export const stateAtom = atom<State | null>(null);
export const metricsAtom = atom<Metrics | null>(null);
export const healthAtom = atom<Health | null>(null);
export const powerAtom = atom<Power | null>(null);
/** Timestamp (seconds) when latest health WS update was applied */

/**
 * Device information (fetched once via REST API on mount)
 */
export const deviceInfoAtom = atom<DeviceInfo | null>(null);

/**
 * MQTT status (fetched once via REST API on mount)
 */
export const mqttStatusAtom = atom<MQTTStatus | null>(null);

/**
 * Bootstrap error state - set when initial REST API calls fail
 * Used to show "cannot reach device" UI instead of infinite loading
 */
export const bootstrapErrorAtom = atom<string | null>(null);

/**
 * OTA update progress (updated via WebSocket during OTA)
 * Sticky across reconnects - keeps last progress state
 */
export const otaProgressAtom = atom<OTAProgress | null>(null);

/**
 * OTA version info (fetched via REST API)
 */
export const otaVersionInfoAtom = atom<OTAVersionInfo | null>(null);

/**
 * Sensor cadences (fetched once on Sensors tab)
 * Record of sensor -> { ms, from_nvs }
 */
export const cadencesAtom = atom<Record<SensorId, SensorCadence> | null>(null);

/**
 * Chart buffers version atom - primitive writable atom
 * Incremented by ChartBufferStream after appending data to buffers
 * Charts subscribe to this for efficient update triggering
 *
 * This is a writable atom (not derived) to avoid race conditions:
 * the version is set AFTER data is appended, ensuring charts always
 * see the latest data when they re-render.
 */
export const buffersVersionAtom = atom<number>(0);

// ============================================================================
// DERIVED ATOMS - Computed Values
// ============================================================================

// Removed derived color atoms (aqiColorAtom, comfortColorAtom, iaqColorAtom)
// Colors now derive in components with theme.vars to react instantly to mode changes.

/**
 * Sensor status map atom - extracts sensor status map from health data
 * Returns null if health data not available yet
 */
export const sensorStatusMapAtom = atom((get) => {
  const health = get(healthAtom);
  return health?.sensors ?? null;
});

// (Deprecated) Prior aggregate connection status has been replaced by fine‑grained atoms

/**
 * Data loading state atom - determines if initial data has loaded
 * Returns true if we have at least state, metrics, or health data
 * Optimized with selectAtom to prevent re-renders after value becomes true
 */
const _dataLoadedAtom = atom((get) => {
  const state = get(stateAtom);
  const metrics = get(metricsAtom);
  const health = get(healthAtom);
  return state !== null || metrics !== null || health !== null;
});

export const dataLoadedAtom = selectAtom(
  _dataLoadedAtom,
  (v) => v,
  (a, b) => a === b  // Strict equality for boolean
);

/**
 * App ready state atom - determines if app is ready to display UI
 * Returns true if device info is loaded (indicates REST API is working)
 * Optimized with selectAtom to prevent re-renders after value becomes true
 */
const _appReadyAtom = atom((get) => {
  const deviceInfo = get(deviceInfoAtom);
  const dataLoaded = get(dataLoadedAtom);
  return deviceInfo !== null || dataLoaded; // Render when either REST or WS data is available
});

export const appReadyAtom = selectAtom(
  _appReadyAtom,
  (v) => v,
  (a, b) => a === b  // Strict equality for boolean
);

/**
 * Individual connection atoms for finer-grained subscriptions
 */
export const wifiConnectedAtom = selectAtom(
  deviceInfoAtom,
  (info) => info?.network?.wifi_connected ?? false,
  (a, b) => a === b
);

export const mqttConnectedAtom = selectAtom(
  atom((get) => {
    const mqtt = get(mqttStatusAtom);
    if (mqtt) return mqtt.connected;
    const info = get(deviceInfoAtom);
    return info?.network?.mqtt_connected ?? false;
  }),
  (v) => v,
  (a, b) => a === b
);

// Refresh actions
export const refreshDeviceInfoAtom = atom(null, async (_get, set) => {
  const info = await apiClient.getInfo();
  set(deviceInfoAtom, info);
});

export const refreshMQTTStatusAtom = atom(null, async (_get, set) => {
  const status = await apiClient.getMQTTStatus();
  set(mqttStatusAtom, status);
});

export const refreshOTAInfoAtom = atom(null, async (_get, set) => {
  const info = await apiClient.getOTAInfo();
  set(otaVersionInfoAtom, info);
});

// ============================================================================
// HELPER DERIVED ATOMS
// ============================================================================

/**
 * Shallow equality check for objects with primitive values.
 * Used by selectAtom to prevent re-renders when object contents are identical.
 */
function shallowEqual<T extends Record<string, unknown>>(a: T | null, b: T | null): boolean {
  if (a === b) return true;
  if (a === null || b === null) return false;
  const keysA = Object.keys(a);
  const keysB = Object.keys(b);
  if (keysA.length !== keysB.length) return false;
  for (const key of keysA) {
    if (a[key] !== b[key]) return false;
  }
  return true;
}

/**
 * Sensor status by ID atom (uses atomFamily for stable atom references)
 * Returns the status for a specific sensor from the health data
 */
export const sensorStatusAtom = atomFamily((sensorId: SensorId) =>
  atom((get) => {
    const sensors = get(sensorStatusMapAtom);
    return sensors?.[sensorId] ?? null;
  })
);

// ============================================================================
// POWER DERIVED ATOMS - Fine-grained subscriptions to reduce re-renders
// ============================================================================

/**
 * Battery display fields (used by BatteryStatus.tsx)
 * Optimized to only re-render when battery-specific values change
 */
export const batteryDisplayAtom = selectAtom(
  powerAtom,
  (p) => p ? {
    charge_pct: p.charge_pct,
    batt_mv: p.batt_mv,
    batt_ma: p.batt_ma,
    charging_on: p.charging_on,
    health_pct: p.health_pct,
    cycles: p.cycles,
    time_left_min: p.time_left_min,
    batt_temp_c: p.batt_temp_c,
  } : null,
  shallowEqual
);

/**
 * Supply info fields (used by SupplyStatus.tsx)
 */
export const supplyDisplayAtom = selectAtom(
  powerAtom,
  (p) => p ? {
    supply_good: p.supply_good,
    supply_mv: p.supply_mv,
    supply_ma: p.supply_ma,
    maintain_mv: p.maintain_mv,
  } : null,
  shallowEqual
);

/**
 * Charging config fields (used by ChargingConfig.tsx)
 */
export const chargingConfigAtom = selectAtom(
  powerAtom,
  (p) => p ? {
    charging_on: p.charging_on,
    charge_limit_ma: p.charge_limit_ma,
    maintain_mv: p.maintain_mv,
  } : null,
  shallowEqual
);

/**
 * Alarm config fields (used by BatteryAlarms.tsx)
 */
export const alarmConfigAtom = selectAtom(
  powerAtom,
  (p) => p ? {
    alarm_low_v_mv: p.alarm_low_v_mv,
    alarm_high_v_mv: p.alarm_high_v_mv,
    alarm_low_pct: p.alarm_low_pct,
  } : null,
  shallowEqual
);

/**
 * Power outputs fields (used by PowerOutputs.tsx)
 */
export const powerOutputsAtom = selectAtom(
  powerAtom,
  (p) => p ? {
    en: p.en,
    v3v_on: p.v3v_on,
    vsqt_on: p.vsqt_on,
    stat_on: p.stat_on,
  } : null,
  shallowEqual
);

/**
 * Power availability (used by PowerDashboard.tsx)
 */
export const powerAvailableAtom = selectAtom(
  powerAtom,
  (p) => p ? { available: p.available, error: p.error } : null,
  shallowEqual
);

// ============================================================================
// STATE DERIVED ATOMS - Fine-grained subscriptions
// ============================================================================

/**
 * MCU temperature atom (used by SystemHealth.tsx)
 * Prevents SystemHealth from re-rendering on every state update
 */
export const mcuTempAtom = selectAtom(
  stateAtom,
  (s) => s?.mcu_temp_c ?? null,
  (a, b) => a === b
);

/**
 * WiFi signal strength category atom
 * Categorizes WiFi signal strength: Excellent (>=-50), Good (>=-60), Fair (>=-70), Poor (<-70)
 */
export const wifiSignalCategoryAtom = atom((get) => {
  const rssi = get(healthAtom)?.wifi_rssi ?? null;
  if (rssi == null) return 'Unknown';
  if (rssi >= -50) return 'Excellent';
  if (rssi >= -60) return 'Good';
  if (rssi >= -70) return 'Fair';
  return 'Poor';
});

// ============================================================================
// WEB CONSOLE ATOMS
// ============================================================================

/**
 * Console bearer token (persisted to localStorage)
 * Used for authentication with /ws/log and /ws/console endpoints
 */
export const consoleTokenAtom = atomWithStorage<string>('iaq-console-token', '');

/**
 * Console toggle state (session-only)
 * Starts disabled every visit to avoid auto-reconnecting
 */
export const consoleEnabledAtom = atom<boolean>(false);
