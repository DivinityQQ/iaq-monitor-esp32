import { useEffect } from 'react';
import { useAtomValue, useSetAtom } from 'jotai';
import { apiClient } from '../../api/client';
import { ChartBufferStream } from '../Charts/ChartBufferStream';
import { WebSocketBridge } from './WebSocketBridge';
import { appReadyAtom, appErrorAtom, deviceInfoAtom, mqttStatusAtom } from '../../store/atoms';
import { logger } from '../../utils/logger';

/**
 * Centralized data bootstrap & streaming layer.
 * - Establishes WS bridge and streams chart buffers
 * - Fetches REST bootstrap data (device info, MQTT status)
 * - Manages global app error state
 */
export function DataLayer() {
  const setDeviceInfo = useSetAtom(deviceInfoAtom);
  const setMqttStatus = useSetAtom(mqttStatusAtom);
  const setAppError = useSetAtom(appErrorAtom);
  const appReady = useAtomValue(appReadyAtom);

  // Initial REST fetch
  useEffect(() => {
    let cancelled = false;
    const fetchInitial = async () => {
      try {
        const [info, mqtt] = await Promise.all([
          apiClient.getInfo(),
          apiClient.getMQTTStatus(),
        ]);
        if (cancelled) return;
        setDeviceInfo(info);
        setMqttStatus(mqtt);
      } catch (err) {
        logger.error('Failed to fetch initial data:', err);
        if (!cancelled) setAppError('Failed to connect to device. Please check your connection.');
      }
    };
    fetchInitial();
    return () => { cancelled = true; };
  }, [setDeviceInfo, setMqttStatus, setAppError]);

  // Clear error once the app has data (via REST or WS)
  useEffect(() => {
    if (appReady) setAppError(null);
  }, [appReady, setAppError]);

  return (
    <>
      <WebSocketBridge />
      <ChartBufferStream />
    </>
  );
}

