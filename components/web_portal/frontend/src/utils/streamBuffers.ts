import type { State } from '../api/types';
import { logger } from './logger';
import { STREAM_BUFFER_CAPACITY } from './constants';

export type MetricKey = 'temp_c' | 'rh_pct' | 'co2_ppm' | 'pm25_ugm3' | 'pm1_ugm3' | 'pm10_ugm3';

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

  /** O(n) linear array extraction (for uPlot compatibility) - only called when rendering */
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
  pm25_ugm3: new RingBuffer(STREAM_BUFFER_CAPACITY),
  pm1_ugm3: new RingBuffer(STREAM_BUFFER_CAPACITY),
  pm10_ugm3: new RingBuffer(STREAM_BUFFER_CAPACITY),
};

/** Version counter that increments on each append - for signaling chart updates */
let buffersVersion = 0;

/**
 * Append new state to all ring buffers
 * O(1) operation - no array shifts or splices
 * Validates input to prevent NaN/Infinity corruption
 */
export function appendStateToBuffers(s: State): void {
  // Input validation - prevent NaN/Infinity from poisoning buffers
  const now = Date.now() / 1000;
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
  seriesRings.pm25_ugm3.push(sanitize(s.pm25_ugm3));
  seriesRings.pm1_ugm3.push(sanitize(s.pm1_ugm3));
  seriesRings.pm10_ugm3.push(sanitize(s.pm10_ugm3));

  buffersVersion++;
}

/**
 * Get buffers as linear arrays for uPlot
 * Returns unwrapped ring buffer contents
 */
export function getBuffers(key: MetricKey): { x: number[]; y: (number | null)[] } {
  return {
    x: timesRing.toArray() as number[],
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
}
