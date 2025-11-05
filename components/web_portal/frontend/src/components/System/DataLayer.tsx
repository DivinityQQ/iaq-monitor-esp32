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

  // Initial REST fetch (independent calls so one failure doesn't block the other)
  useEffect(() => {
    let cancelled = false;
    const fetchInitial = async () => {
      let infoOk = false;
      let mqttOk = false;

      // Fetch device info
      try {
        const info = await apiClient.getInfo();
        if (!cancelled) {
          setDeviceInfo(info);
          infoOk = true;
        }
      } catch (err) {
        logger.error('Failed to fetch device info:', err);
      }

      // Fetch MQTT status
      try {
        const mqtt = await apiClient.getMQTTStatus();
        if (!cancelled) {
          setMqttStatus(mqtt);
          mqttOk = true;
        }
      } catch (err) {
        logger.error('Failed to fetch MQTT status:', err);
      }

      // Set an error banner only if both failed
      if (!cancelled && !infoOk && !mqttOk) {
        setAppError('Unable to reach device. UI will remain in offline mode.');
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

