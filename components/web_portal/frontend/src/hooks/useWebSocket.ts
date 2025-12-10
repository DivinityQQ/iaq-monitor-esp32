import { useEffect } from 'react';
import { useSetAtom } from 'jotai';
import useWebSocket, { ReadyState } from 'react-use-websocket';
import type { WSMessage } from '../api/types';
import {
  wsConnectedAtom,
  wsReconnectingAtom,
  stateAtom,
  metricsAtom,
  healthAtom,
  powerAtom,
  otaProgressAtom,
} from '../store/atoms';
import { logger } from '../utils/logger';
import { buildWsUrl } from '../utils/constants';

/**
 * Custom hook for WebSocket connection to ESP32 device
 *
 * Features:
 * - Exponential backoff reconnection (1s → 10s cap)
 * - Infinite reconnection attempts
 * - Heartbeat mechanism (ping every 30s)
 * - Automatic message parsing and state updates via Jotai
 * - Connection state management
 *
 * @returns WebSocket connection state and methods
 */
export function useWebSocketConnection() {
  // Jotai atom setters
  const setWsConnected = useSetAtom(wsConnectedAtom);
  const setWsReconnecting = useSetAtom(wsReconnectingAtom);
  const setState = useSetAtom(stateAtom);
  const setMetrics = useSetAtom(metricsAtom);
  const setHealth = useSetAtom(healthAtom);
  const setPower = useSetAtom(powerAtom);
  const setOTAProgress = useSetAtom(otaProgressAtom);

  // WebSocket connection with react-use-websocket
  const { sendMessage, lastMessage, readyState } = useWebSocket(
    buildWsUrl('/ws'),
    {
      // Reconnection strategy: exponential backoff with 10s cap
      shouldReconnect: () => true, // Always reconnect
      reconnectAttempts: Infinity, // Never give up
      reconnectInterval: (attemptNumber) =>
        Math.min(Math.pow(2, attemptNumber) * 1000, 10000), // 1s, 2s, 4s, 8s, 10s, 10s...

      // Connection lifecycle callbacks
      onOpen: () => {
        logger.log('[WebSocket] Connected to ESP32');
        setWsConnected(true);
        setWsReconnecting(false);
      },

      onClose: () => {
        logger.log('[WebSocket] Disconnected from ESP32');
        setWsConnected(false);
      },

      onError: (event) => {
        logger.error('[WebSocket] Connection error:', event);
        setWsReconnecting(true);
      },

      onReconnectStop: (numAttempts) => {
        logger.error(`[WebSocket] Failed to reconnect after ${numAttempts} attempts`);
        setWsReconnecting(false);
      },

      // Message handling
      onMessage: (event) => {
        try {
          const message = JSON.parse(event.data) as WSMessage;
          handleMessage(message);
        } catch (error) {
          logger.error('[WebSocket] Failed to parse message:', error, event.data);
        }
      },

      // No app-level heartbeat; server handles protocol PING/PONG

      // Share WebSocket instance across multiple useWebSocket calls
      share: true,
    },
  );

  /**
   * Handle incoming WebSocket message
   * Routes message to appropriate Jotai atom based on type
   */
  const handleMessage = (message: WSMessage) => {
    switch (message.type) {
      case 'state':
        setState(message.data);
        break;
      case 'metrics':
        setMetrics(message.data);
        break;
      case 'health':
        setHealth(message.data);
        break;
      case 'power':
        setPower(message.data);
        break;
      case 'ota_progress':
        setOTAProgress(message.data);
        break;
      default:
        logger.warn('[WebSocket] Unknown message type:', message);
    }
  };

  /**
   * Update reconnecting state based on readyState
   */
  useEffect(() => {
    const isReconnecting = readyState === ReadyState.CONNECTING;
    setWsReconnecting(isReconnecting);
  }, [readyState, setWsReconnecting]);

  // No cleanup needed for heartbeat timers

  return {
    sendMessage,
    lastMessage,
    readyState,
    isConnected: readyState === ReadyState.OPEN,
    isConnecting: readyState === ReadyState.CONNECTING,
  };
}
