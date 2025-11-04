import { useWebSocketConnection } from '../../hooks/useWebSocket';

/**
 * Bridges WebSocket events into Jotai atoms without causing app-wide re-renders.
 * Mount once at the app root.
 */
export function WebSocketBridge() {
  useWebSocketConnection();
  return null;
}
