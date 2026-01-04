import type { State, Metrics } from '../api/types';
import { logger } from './logger';
import { STREAM_BUFFER_CAPACITY } from './constants';
import type { MetricKey } from '../components/Charts/config/chartConfig';

export const STREAM_BUFFER_SECONDS = 3600;

/**
 * Ring buffer implementation for O(1) append/trim operations
 * Avoids O(n) array shifts that were happening 7x per second after buffer fills
 */
class RingBuffer {
  private buffer: (number | null)[];
  private head: number = 0; // Write position
  private size: number = 0; // Current size
  private readonly capacity: number;

  constructor(capacity: number) {
    this.capacity = capacity;
    this.buffer = new Array(capacity);
  }

  /** O(1) append - overwrites oldest when full */
  push(value: number | null): void {
    this.buffer[this.head] = value;
    this.head = (this.head + 1) % this.capacity;
    if (this.size < this.capacity) {
      this.size++;
    }
  }

  /** Get current size */
  length(): number {
    return this.size;
  }

  /** O(n) linear array extraction - only called when rendering */
  toArray(): (number | null)[] {
    if (this.size === 0) return [];
    if (this.size < this.capacity) {
      // Not full yet - return contiguous slice
      return this.buffer.slice(0, this.size);
    }
    // Full - need to unwrap the ring
    const tail = this.head; // Oldest element position
    return [...this.buffer.slice(tail), ...this.buffer.slice(0, tail)];
  }

  /** Get latest value - O(1) */
  latest(): number | null {
    if (this.size === 0) return null;
    const lastIdx = (this.head - 1 + this.capacity) % this.capacity;
    return this.buffer[lastIdx];
  }

  /** Reset buffer (for tests) */
  reset(): void {
    this.head = 0;
    this.size = 0;
    this.buffer.fill(null);
  }
}

// Ring buffers for time and each metric
const timesRing = new RingBuffer(STREAM_BUFFER_CAPACITY);
const seriesRings: Record<MetricKey, RingBuffer> = {
  temp_c: new RingBuffer(STREAM_BUFFER_CAPACITY),
  rh_pct: new RingBuffer(STREAM_BUFFER_CAPACITY),
  co2_ppm: new RingBuffer(STREAM_BUFFER_CAPACITY),
  pressure_hpa: new RingBuffer(STREAM_BUFFER_CAPACITY),
  voc_index: new RingBuffer(STREAM_BUFFER_CAPACITY),
  nox_index: new RingBuffer(STREAM_BUFFER_CAPACITY),
  aqi: new RingBuffer(STREAM_BUFFER_CAPACITY),
  comfort_score: new RingBuffer(STREAM_BUFFER_CAPACITY),
  pm25_ugm3: new RingBuffer(STREAM_BUFFER_CAPACITY),
  pm1_ugm3: new RingBuffer(STREAM_BUFFER_CAPACITY),
  pm10_ugm3: new RingBuffer(STREAM_BUFFER_CAPACITY),
  mold_risk: new RingBuffer(STREAM_BUFFER_CAPACITY),
  iaq_score: new RingBuffer(STREAM_BUFFER_CAPACITY),
};

/** Version counter that increments on each append - for signaling chart updates */
let buffersVersion = 0;
let cachedTimes: number[] = [];
let cachedTimesVersion = -1;

let cachedMetrics: { mold_risk: number | null; iaq_score: number | null } = {
  mold_risk: null,
  iaq_score: null,
};

let timeOffsetSeconds: number | null = null;

export function updateTimeOffset(epochSeconds: number | null): void {
  if (!epochSeconds || !Number.isFinite(epochSeconds)) return;
  const offset = epochSeconds - Date.now() / 1000;
  if (!Number.isFinite(offset)) return;
  timeOffsetSeconds = offset;
}

export function updateMetricsCache(metrics: Metrics): void {
  cachedMetrics = {
    mold_risk: metrics.mold_risk?.score ?? null,
    iaq_score: metrics.overall_iaq_score ?? null,
  };
}

/**
 * Append new state to all ring buffers
 * O(1) operation - no array shifts or splices
 * Validates input to prevent NaN/Infinity corruption
 */
export function appendStateToBuffers(s: State): void {
  // Input validation - prevent NaN/Infinity from poisoning buffers
  const baseNow = Date.now() / 1000;
  const now = Math.floor(baseNow + (timeOffsetSeconds ?? 0));
  if (!Number.isFinite(now)) {
    logger.warn('[streamBuffers] Invalid timestamp, skipping append');
    return;
  }

  // Helper to sanitize numeric values
  const sanitize = (val: number | null | undefined): number | null => {
    if (val == null) return null;
    if (!Number.isFinite(val)) {
      logger.warn('[streamBuffers] Non-finite value detected:', val);
      return null;
    }
    return val;
  };

  timesRing.push(now);
  seriesRings.temp_c.push(sanitize(s.temp_c));
  seriesRings.rh_pct.push(sanitize(s.rh_pct));
  seriesRings.co2_ppm.push(sanitize(s.co2_ppm));
  seriesRings.pressure_hpa.push(sanitize(s.pressure_hpa));
  seriesRings.voc_index.push(sanitize(s.voc_index));
  seriesRings.nox_index.push(sanitize(s.nox_index));
  seriesRings.aqi.push(sanitize(s.aqi));
  seriesRings.comfort_score.push(sanitize(s.comfort_score));
  seriesRings.pm25_ugm3.push(sanitize(s.pm25_ugm3));
  seriesRings.pm1_ugm3.push(sanitize(s.pm1_ugm3));
  seriesRings.pm10_ugm3.push(sanitize(s.pm10_ugm3));
  seriesRings.mold_risk.push(sanitize(cachedMetrics.mold_risk));
  seriesRings.iaq_score.push(sanitize(cachedMetrics.iaq_score));

  buffersVersion++;
}

/**
 * Get buffers as linear arrays for chart rendering
 * Returns unwrapped ring buffer contents
 */
export function getBuffers(key: MetricKey): { x: number[]; y: (number | null)[] } {
  return {
    x: getTimesArray(),
    y: seriesRings[key].toArray(),
  };
}

/**
 * Get latest value for a metric - O(1)
 */
export function getLatest(key: MetricKey): number | null {
  const v = seriesRings[key].latest();
  return v == null || Number.isNaN(v) ? null : (v as number);
}

export function getLatestTimestamp(): number | null {
  return timesRing.latest();
}

export function getTimesArray(): number[] {
  if (cachedTimesVersion !== buffersVersion) {
    cachedTimes = timesRing.toArray() as number[];
    cachedTimesVersion = buffersVersion;
  }
  return cachedTimes;
}

/**
 * Get current version counter (for change detection)
 */
export function getBuffersVersion(): number {
  return buffersVersion;
}

/**
 * Reset all buffers (for testing)
 */
export function resetBuffers(): void {
  timesRing.reset();
  (Object.keys(seriesRings) as MetricKey[]).forEach((k) => seriesRings[k].reset());
  buffersVersion = 0;
  cachedTimes = [];
  cachedTimesVersion = -1;
}
