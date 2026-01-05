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

export function formatAxisTick(value: number, defaultTickLabel: string): string {
  if (!Number.isFinite(value)) return '';
  return defaultTickLabel;
}

/**
 * Computes Y-axis bounds using single-pass min/max calculation.
 * Avoids spread operator which can cause stack overflow on large arrays.
 */
export function computeYAxisBounds(
  data: ChartDataPoint[],
  config: MetricConfig,
  includeMinMax: boolean = false
): { min: number; max: number } {
  // Single-pass min/max calculation - O(n) without stack overflow risk
  let dataMin = Infinity;
  let dataMax = -Infinity;

  const addValue = (value: number | null | undefined) => {
    if (value == null || !Number.isFinite(value)) return;
    if (value < dataMin) dataMin = value;
    if (value > dataMax) dataMax = value;
  };

  for (let i = 0; i < data.length; i++) {
    const point = data[i];
    addValue(point.avg);
    if (includeMinMax) {
      addValue(point.min);
      addValue(point.max);
    }
  }

  const hasData = dataMin !== Infinity;

  if (!hasData) {
    const min = config.yMin ?? 0;
    const max = config.yMax ?? config.softYMax ?? 100;
    return { min, max };
  }

  let min = dataMin;
  let max = dataMax;

  if (min === max) {
    const pad = Math.max(1, Math.abs(min) * 0.05);
    min -= pad;
    max += pad;
  } else {
    const pad = (max - min) * 0.05;
    min -= pad;
    max += pad;
  }

  if (config.yMin !== undefined) {
    min = Math.max(min, config.yMin);
  }

  if (min >= max) {
    const fallbackMin = config.yMin ?? min;
    const fallbackMax = fallbackMin + 1;
    return { min: fallbackMin, max: fallbackMax };
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

export function buildXAxisTicks(rangeSeconds: number): number[] | undefined {
  if (rangeSeconds === 86400) {
    return buildTickSeries(rangeSeconds, 7200);
  }
  if (rangeSeconds === 604800) {
    return buildTickSeries(rangeSeconds, 86400);
  }
  return undefined;
}

function buildTickSeries(rangeSeconds: number, stepSeconds: number): number[] {
  const ticks: number[] = [];
  for (let t = -rangeSeconds; t <= 0; t += stepSeconds) {
    ticks.push(t);
  }
  if (ticks[ticks.length - 1] !== 0) ticks.push(0);
  return ticks;
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
