import { useEffect, useRef } from 'react';
import { useAtomValue, useSetAtom } from 'jotai';
import type { Metrics, State } from '../../api/types';
import { buffersVersionAtom, healthAtom, metricsAtom, stateAtom } from '../../store/atoms';
import { appendStateToBuffers, updateMetricsCache, updateTimeOffset } from '../../utils/streamBuffers';

/**
 * Background streamer that mirrors stateAtom samples into global chart buffers.
 * Mount once at app level so history persists across route changes.
 * Uses ref guard to prevent double-append in React StrictMode (effects run twice in dev).
 *
 * After appending data, increments buffersVersionAtom to trigger chart re-renders.
 * This ensures charts always see the latest data (no race condition).
 */
export function ChartBufferStream() {
  const state = useAtomValue(stateAtom);
  const metrics = useAtomValue(metricsAtom);
  const health = useAtomValue(healthAtom);
  const setBuffersVersion = useSetAtom(buffersVersionAtom);
  const lastStateRef = useRef<State | null>(null);
  const lastMetricsRef = useRef<Metrics | null>(null);
  const lastEpochRef = useRef<number | null>(null);

  useEffect(() => {
    // Guard against StrictMode double-invocation and duplicate state objects
    if (state && state !== lastStateRef.current) {
      lastStateRef.current = state;
      appendStateToBuffers(state);
      // Increment version AFTER data is appended - triggers chart re-renders
      setBuffersVersion((v) => v + 1);
    }
  }, [state, setBuffersVersion]);

  useEffect(() => {
    if (metrics && metrics !== lastMetricsRef.current) {
      lastMetricsRef.current = metrics;
      updateMetricsCache(metrics);
    }
  }, [metrics]);

  useEffect(() => {
    const epoch = health?.time_synced ? health.epoch ?? null : null;
    if (!epoch || epoch === lastEpochRef.current) return;
    lastEpochRef.current = epoch;
    updateTimeOffset(epoch);
  }, [health?.epoch, health?.time_synced]);

  return null;
}
