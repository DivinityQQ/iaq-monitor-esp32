import { atom } from 'jotai';
import type { State, Metrics, Health, DeviceInfo, SensorId, SensorCadence } from '../api/types';
import { getAQIColor, getComfortColor, getIAQColor } from '../theme';

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
 * - Health: 30s updates + WiFi events (system health, uptime, sensors)
 */
export const stateAtom = atom<State | null>(null);
export const metricsAtom = atom<Metrics | null>(null);
export const healthAtom = atom<Health | null>(null);
/** Timestamp (seconds) when latest health WS update was applied */

/**
 * Device information (fetched once via REST API on mount)
 */
export const deviceInfoAtom = atom<DeviceInfo | null>(null);

/**
 * Sensor cadences (fetched once on Sensors tab)
 * Record of sensor -> { ms, from_nvs }
 */
export const cadencesAtom = atom<Record<SensorId, SensorCadence> | null>(null);

// ============================================================================
// DERIVED ATOMS - Computed Values
// ============================================================================

/**
 * AQI color atom - derives color from current AQI value
 * Uses getAQIColor helper from theme.ts
 */
export const aqiColorAtom = atom((get) => {
  const metrics = get(metricsAtom);
  return getAQIColor(metrics?.aqi?.value);
});

/**
 * Comfort color atom - derives color from current comfort score
 * Uses getComfortColor helper from theme.ts
 */
export const comfortColorAtom = atom((get) => {
  const metrics = get(metricsAtom);
  return getComfortColor(metrics?.comfort?.score);
});

/**
 * IAQ color atom - derives color from current overall IAQ score
 * Uses getIAQColor helper from theme.ts
 */
export const iaqColorAtom = atom((get) => {
  const metrics = get(metricsAtom);
  return getIAQColor(metrics?.overall_iaq_score);
});

/**
 * Sensor status map atom - extracts sensor status map from health data
 * Returns null if health data not available yet
 */
export const sensorStatusMapAtom = atom((get) => {
  const health = get(healthAtom);
  return health?.sensors ?? null;
});

/**
 * Connection status atom - combines all connection indicators
 * Derives WebSocket, WiFi, and MQTT connection states
 */
export const connectionStatusAtom = atom((get) => {
  const wsConnected = get(wsConnectedAtom);
  const deviceInfo = get(deviceInfoAtom);
  const health = get(healthAtom);

  return {
    websocket: wsConnected,
    wifi: deviceInfo?.network?.wifi_connected ?? false,
    mqtt: deviceInfo?.network?.mqtt_connected ?? false,
    rssi: health?.wifi_rssi ?? null,
  };
});

/**
 * Data loading state atom - determines if initial data has loaded
 * Returns true if we have at least state or metrics data
 */
export const dataLoadedAtom = atom((get) => {
  const state = get(stateAtom);
  const metrics = get(metricsAtom);
  return state !== null || metrics !== null;
});

/**
 * App ready state atom - determines if app is ready to display UI
 * Returns true if device info is loaded (indicates REST API is working)
 */
export const appReadyAtom = atom((get) => {
  const deviceInfo = get(deviceInfoAtom);
  const dataLoaded = get(dataLoadedAtom);
  return deviceInfo !== null || dataLoaded; // Render when either REST or WS data is available
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
 * Categorizes WiFi signal strength: Excellent (>-50), Good (>-60), Fair (>-70), Poor (≤-70)
 */
export const wifiSignalCategoryAtom = atom((get) => {
  const status = get(connectionStatusAtom);
  if (!status.rssi) return 'Unknown';
  if (status.rssi > -50) return 'Excellent';
  if (status.rssi > -60) return 'Good';
  if (status.rssi > -70) return 'Fair';
  return 'Poor';
});
