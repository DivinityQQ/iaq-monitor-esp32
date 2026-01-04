import type { Theme } from '@mui/material/styles';
import type { MetricConfig } from '../config/chartConfig';
import type { ChartDataPoint } from '../types';

export function resolvePaletteColor(theme: Theme, path: string): string {
  const [paletteKey, shade] = path.split('.');
  const paletteEntry = (theme.palette as Record<string, any>)[paletteKey];
  if (paletteEntry && shade && paletteEntry[shade]) return paletteEntry[shade];
  if (paletteEntry?.main) return paletteEntry.main;
  return path;
}

/**
 * Creates a formatter for relative time axis labels.
 * Values are already relative (negative offsets from 0), so just format them.
 */
export function createRelativeTimeFormatter(rangeSeconds: number) {
  return (value: number): string => {
    const delta = Math.max(0, Math.round(Math.abs(value)));
    if (rangeSeconds <= 90) return `-${delta}s`;
    if (rangeSeconds <= 600) {
      const m = Math.floor(delta / 60);
      const s = String(delta % 60).padStart(2, '0');
      return `-${m}:${s}`;
    }
    if (rangeSeconds <= 3600) {
      const m = Math.round(delta / 60);
      return `-${m}m`;
    }
    if (rangeSeconds <= 86400) {
      const h = Math.floor(delta / 3600);
      const m = Math.round((delta % 3600) / 60);
      return m > 0 ? `-${h}h${m}m` : `-${h}h`;
    }
    const d = Math.floor(delta / 86400);
    const h = Math.round((delta % 86400) / 3600);
    return h > 0 ? `-${d}d${h}h` : `-${d}d`;
  };
}

/**
 * Computes Y-axis bounds using single-pass min/max calculation.
 * Avoids spread operator which can cause stack overflow on large arrays.
 */
export function computeYAxisBounds(
  data: ChartDataPoint[],
  config: MetricConfig
): { min: number; max: number } {
  // Single-pass min/max calculation - O(n) without stack overflow risk
  let dataMin = Infinity;
  let dataMax = -Infinity;

  for (let i = 0; i < data.length; i++) {
    const v = data[i].avg;
    if (v != null && Number.isFinite(v)) {
      if (v < dataMin) dataMin = v;
      if (v > dataMax) dataMax = v;
    }
  }

  const hasData = dataMin !== Infinity;

  // If both bounds are fixed, use them directly
  if (config.yMin !== undefined && config.yMax !== undefined) {
    return { min: config.yMin, max: config.yMax };
  }

  let min = config.yMin ?? (hasData ? dataMin : 0);
  let max = config.yMax ?? (hasData ? dataMax : config.softYMax ?? 100);

  // Apply soft max if configured
  if (config.softYMax !== undefined) {
    max = Math.max(max, config.softYMax);
  }

  // Extend bounds to include data if not fixed
  if (config.yMin === undefined && hasData) {
    min = Math.min(min, dataMin);
  }

  if (config.yMax === undefined && hasData) {
    max = Math.max(max, dataMax);
  }

  // Add padding for visual breathing room
  if (Number.isFinite(min) && Number.isFinite(max)) {
    const span = Math.max(1, max - min);
    const pad = span * 0.05;
    if (config.yMin === undefined) min -= pad;
    if (config.yMax === undefined && config.softYMax === undefined) max += pad;
  }

  return { min, max };
}

export function windowToRange(
  data: ChartDataPoint[],
  windowEnd: number,
  rangeSeconds: number
): ChartDataPoint[] {
  const windowStart = windowEnd - rangeSeconds;
  return data.filter((point) => point.time >= windowStart && point.time <= windowEnd);
}

export const CHART_LAYOUT = {
  margin: { left: 8, right: 14, top: 6, bottom: 6 },
  xAxis: {
    height: 30,
    tickSize: 4,
    tickLabelMinGap: 6,
  },
  yAxis: {
    width: 48,
    tickSize: 4,
  },
} as const;
