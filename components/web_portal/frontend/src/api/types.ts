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
  stale?: boolean;
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
  /** Last known valid values (for stale display). Fields mirror top-level State fields. */
  last?: {
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
  };
}

// ============================================================================
// METRICS (5s WebSocket updates)
// ============================================================================

export interface MetricsAqi {
  value: number | null;
  category: string | null;
  dominant: string | null;
  pm25_subindex: number | null;
  pm10_subindex: number | null;
}

export interface MetricsComfort {
  score: number | null;
  category: string | null;
  dew_point_c: number | null;
  abs_humidity_gm3: number | null;
  heat_index_c: number | null;
}

export interface MetricsPressure {
  trend: string | null;
  delta_hpa: number | null;
  window_hours: number | null;
}

export interface MetricsMoldRisk {
  score: number | null;
  category: string | null;
}

export interface MetricsLast {
  aqi: MetricsAqi;
  comfort: MetricsComfort;
  pressure: MetricsPressure;
  co2_score: number | null;
  voc_category: string | null;
  nox_category: string | null;
  overall_iaq_score: number | null;
  mold_risk: MetricsMoldRisk;
  co2_rate_ppm_hr: number | null;
  pm25_spike_detected: boolean | null;
}

export interface Metrics {
  aqi: MetricsAqi;
  comfort: MetricsComfort;
  pressure: MetricsPressure;
  co2_score: number | null;
  voc_category: string | null;
  nox_category: string | null;
  overall_iaq_score: number | null;
  mold_risk: MetricsMoldRisk;
  co2_rate_ppm_hr: number | null;
  pm25_spike_detected: boolean | null;
  /** Last known valid metrics (for stale display). Structure mirrors top-level Metrics. */
  last?: MetricsLast;
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
  frontend?: {
    version?: string;
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
// POWER (1Hz WebSocket updates - PowerFeather only)
// ============================================================================

export interface Power {
  available: boolean;
  supply_good: boolean;
  supply_mv: number;
  supply_ma: number;
  maintain_mv: number;
  en: boolean;
  v3v_on: boolean;
  vsqt_on: boolean;
  stat_on: boolean;
  charging_on: boolean;
  charge_limit_ma: number;
  batt_mv: number;
  batt_ma: number;
  charge_pct: number;
  health_pct: number;
  cycles: number;
  time_left_min: number;
  batt_temp_c: number;
  alarm_low_v_mv: number;
  alarm_high_v_mv: number;
  alarm_low_pct: number;
  updated_at_us: number;
  error?: string;
}

export interface PowerOutputsConfig {
  en?: boolean;
  v3v_on?: boolean;
  vsqt_on?: boolean;
  stat_on?: boolean;
}

export interface ChargerConfig {
  enable?: boolean;
  limit_ma?: number;
  maintain_mv?: number;
}

export interface PowerAlarmsConfig {
  low_v_mv?: number;
  high_v_mv?: number;
  low_pct?: number;
}

// ============================================================================
// OTA UPDATE
// ============================================================================

export type OTAState = 'idle' | 'receiving' | 'validating' | 'complete' | 'error';
export type OTAUpdateType = 'firmware' | 'frontend' | 'none';

export interface OTAProgress {
  update_type: OTAUpdateType;
  state: OTAState;
  progress: number;
  received: number;
  total: number;
  error?: string;
}

export interface OTAFirmwareInfo {
  version: string;
  build_date: string;
  build_time: string;
  idf_version: string;
}

export interface OTAFrontendInfo {
  version: string;
}

export interface OTARuntimeInfo {
  state: OTAState;
  update_type: OTAUpdateType;
  active_slot: number;
  rollback_available: boolean;
  pending_verify: boolean;
  received: number;
  total: number;
  error?: string;
}

export interface OTAVersionInfo {
  firmware: OTAFirmwareInfo;
  frontend: OTAFrontendInfo;
  ota: OTARuntimeInfo;
}

export interface OTAUploadResponse {
  status: 'ok' | 'rebooting';
  message: string;
  reboot_required: boolean;
}

// ============================================================================
// WEBSOCKET MESSAGES
// ============================================================================

export type WSMessage =
  | { type: 'state'; data: State }
  | { type: 'metrics'; data: Metrics }
  | { type: 'health'; data: Health }
  | { type: 'power'; data: Power }
  | { type: 'ota_progress'; data: OTAProgress };

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
