import { atom } from 'jotai';
import { selectAtom } from 'jotai/utils';
import type { State, Metrics, Health, Power, DeviceInfo, SensorId, SensorCadence, MQTTStatus, OTAProgress, OTAVersionInfo } from '../api/types';
// Color derivations moved to components using theme CSS variables for live updates
import { getBuffersVersion } from '../utils/streamBuffers';
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
 * Chart buffers version atom - read-only derived atom
 * Increments whenever new data is appended to chart buffers
 * Charts subscribe to this for efficient update triggering
 */
export const buffersVersionAtom = atom((get) => {
  // Also depend on stateAtom to trigger re-evaluation
  get(stateAtom);
  return getBuffersVersion();
});

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
 * Sensor status by ID atom factory
 * Creates a derived atom that extracts status for a specific sensor
 */
export const sensorStatusAtom = (sensorId: SensorId) =>
  atom((get) => {
    const sensors = get(sensorStatusMapAtom);
    return sensors?.[sensorId] ?? null;
  });

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
