// ============================================================================
// SENSOR TYPES
// ============================================================================

export type SensorId = 'mcu' | 'sht45' | 'bmp280' | 'sgp41' | 'pms5003' | 's8';
export type SensorState = 'UNINIT' | 'INIT' | 'WARMING' | 'READY' | 'ERROR' | 'DISABLED';
export type SensorAction = 'read' | 'reset' | 'enable' | 'disable' | 'cadence';

/**
 * Canonical ordered list of all sensor IDs in the system.
 * Use this constant to ensure consistent sensor ordering across UI components.
 */
export const ALL_SENSOR_IDS: readonly SensorId[] = ['mcu', 'sht45', 'bmp280', 'sgp41', 'pms5003', 's8'] as const;

export interface SensorStatus {
  state: SensorState;
  errors: number;
  last_read_s?: number;
  warmup_remaining_s?: number;
}

export interface SensorCadence {
  ms: number;
  from_nvs: boolean;
}

// ============================================================================
// STATE (1Hz WebSocket updates)
// ============================================================================

export interface State {
  temp_c: number | null;
  rh_pct: number | null;
  pressure_hpa: number | null;
  pm25_ugm3: number | null;
  pm10_ugm3: number | null;
  pm1_ugm3?: number | null;
  co2_ppm: number | null;
  voc_index: number | null;
  nox_index: number | null;
  mcu_temp_c: number | null;
  aqi: number | null;
  comfort_score: number | null;
}

// ============================================================================
// METRICS (5s WebSocket updates)
// ============================================================================

export interface Metrics {
  aqi: {
    value: number;
    category: string;
    dominant: string;
    pm25_subindex: number;
    pm10_subindex: number;
  };
  comfort: {
    score: number;
    category: string;
    dew_point_c: number;
    abs_humidity_gm3: number;
    heat_index_c: number;
  };
  pressure: {
    trend: string;
    delta_hpa: number;
    window_hours: number;
  };
  co2_score: number;
  voc_category: string;
  nox_category: string;
  overall_iaq_score: number;
  mold_risk: {
    score: number;
    category: string;
  };
  co2_rate_ppm_hr: number;
  pm25_spike_detected: boolean;
}

// ============================================================================
// HEALTH (1 Hz WebSocket updates while connected + WiFi events)
// ============================================================================

export interface Health {
  uptime: number;
  wifi_rssi: number;
  free_heap: number;
  time_synced: boolean;
  epoch?: number;
  sensors: Record<SensorId, SensorStatus>;
}

// ============================================================================
// DEVICE INFO (REST API: GET /api/v1/info)
// ============================================================================

export interface DeviceInfo {
  device_id: string;
  device: {
    name: string;
    model: string;
    manufacturer: string;
  };
  firmware: {
    version: string;
    build_date: string;
    build_time: string;
    idf_version: string;
    license: string;
  };
  hardware: {
    chip: string;
    cores: number;
    revision: number;
  };
  network: {
    mode: string;
    wifi_connected: boolean;
    mqtt_connected: boolean;
    sta_ip?: string;
    ap_ip?: string;
  };
}

// ============================================================================
// WIFI
// ============================================================================

export interface WiFiStatus {
  provisioned: boolean;
  mode: string;
  ssid: string;
  rssi: number;
}

export interface WiFiAP {
  ssid: string;
  rssi: number;
  channel: number;
  auth: string;
}

export interface WiFiScanResult {
  aps: WiFiAP[];
}

export interface WiFiConfig {
  ssid: string;
  password: string;
  restart?: boolean;
}

// ============================================================================
// MQTT
// ============================================================================

export interface MQTTStatus {
  broker_url: string;
  configured: boolean;
  connected: boolean;
}

export interface MQTTConfig {
  broker_url: string;
  username?: string;
  password?: string;
  restart?: boolean;
}

// ============================================================================
// WEBSOCKET MESSAGES
// ============================================================================

export type WSMessage =
  | { type: 'state'; data: State }
  | { type: 'metrics'; data: Metrics }
  | { type: 'health'; data: Health };

// ============================================================================
// API RESPONSES
// ============================================================================

export interface ApiError {
  error: {
    code: string;
    message: string;
    status: number;
  };
}

export interface ApiSuccess {
  status: 'ok' | 'restarting';
}

export interface CadenceResponse {
  cadences: Record<SensorId, SensorCadence>;
}

export interface SensorsResponse {
  sensors: Record<SensorId, SensorStatus>;
}
