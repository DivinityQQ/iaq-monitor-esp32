import { useEffect } from 'react';
import { useSetAtom } from 'jotai';
import { apiClient } from '../../api/client';
import { ChartBufferStream } from '../Charts/ChartBufferStream';
import { WebSocketBridge } from './WebSocketBridge';
import { deviceInfoAtom, mqttStatusAtom } from '../../store/atoms';
import { logger } from '../../utils/logger';

/**
 * Centralized data bootstrap & streaming layer.
 * - Establishes WS bridge and streams chart buffers
 * - Fetches REST bootstrap data (device info, MQTT status)
 */
export function DataLayer() {
  const setDeviceInfo = useSetAtom(deviceInfoAtom);
  const setMqttStatus = useSetAtom(mqttStatusAtom);

  // Initial REST fetch (independent calls so one failure doesn't block the other)
  useEffect(() => {
    let cancelled = false;
    const fetchInitial = async () => {
      // Fetch device info
      try {
        const info = await apiClient.getInfo();
        if (!cancelled) {
          setDeviceInfo(info);
        }
      } catch (err) {
        logger.error('Failed to fetch device info:', err);
      }

      // Fetch MQTT status
      try {
        const mqtt = await apiClient.getMQTTStatus();
        if (!cancelled) {
          setMqttStatus(mqtt);
        }
      } catch (err) {
        logger.error('Failed to fetch MQTT status:', err);
      }
    };

    fetchInitial();
    return () => { cancelled = true; };
  }, [setDeviceInfo, setMqttStatus]);

  return (
    <>
      <WebSocketBridge />
      <ChartBufferStream />
    </>
  );
}

