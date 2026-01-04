export type MetricKey =
  | 'temp_c'
  | 'rh_pct'
  | 'co2_ppm'
  | 'pressure_hpa'
  | 'pm1_ugm3'
  | 'pm25_ugm3'
  | 'pm10_ugm3'
  | 'voc_index'
  | 'nox_index'
  | 'mold_risk'
  | 'aqi'
  | 'comfort_score'
  | 'iaq_score';

export interface MetricConfig {
  id: MetricKey;
  label: string;
  unit: string;
  color: string; // MUI palette path, e.g. "error.main"
  decimals: number;
  yMin?: number;
  yMax?: number;
  softYMax?: number;
  source: 'state' | 'metrics';
  sourceField: string;
}

export const METRICS: Record<MetricKey, MetricConfig> = {
  temp_c: {
    id: 'temp_c',
    label: 'Temperature',
    unit: '°C',
    color: 'error.main',
    decimals: 2,
    yMin: 15,
    softYMax: 35,
    source: 'state',
    sourceField: 'temp_c',
  },
  rh_pct: {
    id: 'rh_pct',
    label: 'Humidity',
    unit: '%',
    color: 'info.main',
    decimals: 2,
    yMin: 0,
    yMax: 100,
    source: 'state',
    sourceField: 'rh_pct',
  },
  co2_ppm: {
    id: 'co2_ppm',
    label: 'CO₂',
    unit: 'ppm',
    color: 'warning.main',
    decimals: 0,
    yMin: 400,
    softYMax: 2000,
    source: 'state',
    sourceField: 'co2_ppm',
  },
  pressure_hpa: {
    id: 'pressure_hpa',
    label: 'Pressure',
    unit: 'hPa',
    color: 'secondary.main',
    decimals: 2,
    yMin: 900,
    softYMax: 1100,
    source: 'state',
    sourceField: 'pressure_hpa',
  },
  pm1_ugm3: {
    id: 'pm1_ugm3',
    label: 'PM1.0',
    unit: 'µg/m³',
    color: 'info.main',
    decimals: 1,
    yMin: 0,
    softYMax: 50,
    source: 'state',
    sourceField: 'pm1_ugm3',
  },
  pm25_ugm3: {
    id: 'pm25_ugm3',
    label: 'PM2.5',
    unit: 'µg/m³',
    color: 'error.main',
    decimals: 1,
    yMin: 0,
    softYMax: 50,
    source: 'state',
    sourceField: 'pm25_ugm3',
  },
  pm10_ugm3: {
    id: 'pm10_ugm3',
    label: 'PM10',
    unit: 'µg/m³',
    color: 'warning.main',
    decimals: 1,
    yMin: 0,
    softYMax: 50,
    source: 'state',
    sourceField: 'pm10_ugm3',
  },
  voc_index: {
    id: 'voc_index',
    label: 'VOC Index',
    unit: '',
    color: 'success.main',
    decimals: 0,
    yMin: 0,
    yMax: 500,
    source: 'state',
    sourceField: 'voc_index',
  },
  nox_index: {
    id: 'nox_index',
    label: 'NOx Index',
    unit: '',
    color: 'primary.main',
    decimals: 0,
    yMin: 0,
    yMax: 500,
    source: 'state',
    sourceField: 'nox_index',
  },
  mold_risk: {
    id: 'mold_risk',
    label: 'Mold Risk',
    unit: '',
    color: 'warning.main',
    decimals: 0,
    yMin: 0,
    yMax: 100,
    source: 'metrics',
    sourceField: 'mold_risk.score',
  },
  aqi: {
    id: 'aqi',
    label: 'AQI',
    unit: '',
    color: 'primary.main',
    decimals: 0,
    yMin: 0,
    yMax: 500,
    source: 'state',
    sourceField: 'aqi',
  },
  comfort_score: {
    id: 'comfort_score',
    label: 'Comfort',
    unit: '',
    color: 'success.dark',
    decimals: 0,
    yMin: 0,
    yMax: 100,
    source: 'state',
    sourceField: 'comfort_score',
  },
  iaq_score: {
    id: 'iaq_score',
    label: 'IAQ Score',
    unit: '',
    color: 'secondary.dark',
    decimals: 0,
    yMin: 0,
    yMax: 100,
    source: 'metrics',
    sourceField: 'overall_iaq_score',
  },
};

export const SINGLE_SERIES_CHARTS: MetricKey[] = [
  'temp_c',
  'rh_pct',
  'co2_ppm',
  'pressure_hpa',
  'voc_index',
  'nox_index',
  'mold_risk',
  'aqi',
  'comfort_score',
  'iaq_score',
];

export const PM_SERIES: MetricKey[] = ['pm1_ugm3', 'pm25_ugm3', 'pm10_ugm3'];

export type RangeKey = '1m' | '5m' | '1h' | '1d' | '7d';

export const RANGES: Record<RangeKey, { seconds: number; label: string; useHistory: boolean }> = {
  '1m': { seconds: 60, label: '1 min', useHistory: false },
  '5m': { seconds: 300, label: '5 min', useHistory: false },
  '1h': { seconds: 3600, label: '1 hour', useHistory: true },
  '1d': { seconds: 86400, label: '1 day', useHistory: true },
  '7d': { seconds: 604800, label: '7 days', useHistory: true },
};
