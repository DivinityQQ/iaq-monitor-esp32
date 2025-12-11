import { useAtomValue } from 'jotai';
import { sensorStatusMapAtom } from '../store/atoms';
import type { SensorId } from '../api/types';

interface SensorCheck {
  /** Sensor ID to check */
  id: SensorId;
  /** Human-readable label for the sensor (shown in stale message) */
  label: string;
}

interface SensorStaleStatus {
  /** True if any of the checked sensors are stale/error/disabled */
  isStale: boolean;
  /** Formatted stale reason string (e.g., "Stale \u2022 PM, CO\u2082 sensors") */
  staleReason: string;
  /** List of stale sensor labels */
  staleSensors: string[];
}

/**
 * Hook to check if sensors are stale, in error, or disabled.
 * Extracts common stale-detection logic used across featured cards.
 *
 * @param sensors - Array of sensors to check with their display labels
 * @returns Object containing isStale boolean, formatted reason string, and list of stale sensors
 *
 * @example
 * const { isStale, staleReason } = useSensorStaleStatus([
 *   { id: 'pms5003', label: 'PM' },
 *   { id: 's8', label: 'CO\u2082' },
 * ]);
 */
export function useSensorStaleStatus(sensors: SensorCheck[]): SensorStaleStatus {
  const sensorStatusMap = useAtomValue(sensorStatusMapAtom);

  const staleSensors: string[] = [];
  for (const { id, label } of sensors) {
    const status = sensorStatusMap?.[id];
    if (status?.stale || status?.state === 'ERROR' || status?.state === 'DISABLED') {
      staleSensors.push(label);
    }
  }

  const isStale = staleSensors.length > 0;
  // Keep labels as-provided to avoid awkward “sensors sensors” wording when labels already contain “sensor”
  const staleReason = isStale ? `Stale \u2022 ${staleSensors.join(', ')}` : '';

  return { isStale, staleReason, staleSensors };
}
