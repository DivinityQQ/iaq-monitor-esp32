import { useEffect, useRef } from 'react';
import { useSetAtom } from 'jotai';
import { apiClient } from '../../api/client';
import { ChartBufferStream } from '../Charts/ChartBufferStream';
import { WebSocketBridge } from './WebSocketBridge';
import { deviceInfoAtom, mqttStatusAtom, bootstrapErrorAtom } from '../../store/atoms';
import { logger } from '../../utils/logger';

/**
 * Centralized data bootstrap & streaming layer.
 * - Establishes WS bridge and streams chart buffers
 * - Fetches REST bootstrap data (device info, MQTT status)
 * - Sets bootstrap error if device is unreachable
 */
export function DataLayer() {
  const setDeviceInfo = useSetAtom(deviceInfoAtom);
  const setMqttStatus = useSetAtom(mqttStatusAtom);
  const setBootstrapError = useSetAtom(bootstrapErrorAtom);
  const retryTimeoutRef = useRef<ReturnType<typeof setTimeout> | null>(null);

  // Initial REST fetch (independent calls so one failure doesn't block the other)
  useEffect(() => {
    let cancelled = false;
    const fetchInitial = async () => {
      let deviceInfoSuccess = false;
      let mqttStatusSuccess = false;

      // Fetch device info
      try {
        const info = await apiClient.getInfo();
        if (!cancelled) {
          setDeviceInfo(info);
          deviceInfoSuccess = true;
        }
      } catch (err) {
        logger.error('Failed to fetch device info:', err);
      }

      // Fetch MQTT status
      try {
        const mqtt = await apiClient.getMQTTStatus();
        if (!cancelled) {
          setMqttStatus(mqtt);
          mqttStatusSuccess = true;
        }
      } catch (err) {
        logger.error('Failed to fetch MQTT status:', err);
      }

      // If both failed, set error state and schedule a retry; otherwise clear errors
      if (!cancelled && !deviceInfoSuccess && !mqttStatusSuccess) {
        setBootstrapError('Unable to reach device. Check your connection.');
        if (!retryTimeoutRef.current) {
          retryTimeoutRef.current = setTimeout(() => {
            retryTimeoutRef.current = null;
            fetchInitial();
          }, 5000);
        }
      } else if (!cancelled) {
        // Clear any previous error and cancel pending retry
        setBootstrapError(null);
        if (retryTimeoutRef.current) {
          clearTimeout(retryTimeoutRef.current);
          retryTimeoutRef.current = null;
        }
      }
    };

    fetchInitial();
    return () => {
      cancelled = true;
      if (retryTimeoutRef.current) {
        clearTimeout(retryTimeoutRef.current);
        retryTimeoutRef.current = null;
      }
    };
  }, [setDeviceInfo, setMqttStatus, setBootstrapError]);

  return (
    <>
      <WebSocketBridge />
      <ChartBufferStream />
    </>
  );
}
