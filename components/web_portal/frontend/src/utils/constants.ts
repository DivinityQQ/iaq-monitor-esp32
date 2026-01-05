/**
 * Application-wide constants
 * Centralizes magic numbers and configuration values for easier maintenance
 */

// Stream Buffer Configuration
/** Ring buffer capacity: 5 minutes @ 1 Hz with cushion (300 + 20) */
export const STREAM_BUFFER_CAPACITY = 320;

// Timeout Values (milliseconds)
/** Default notification display duration */
export const NOTIFICATION_DURATION = 6000;

/** Restart notification display duration */
export const RESTART_NOTIFICATION_DURATION = 4000;

/** MQTT status refresh delay after restart */
export const MQTT_REFRESH_DELAY = 2000;

// WebSocket Configuration
/** Maximum reconnection interval (milliseconds) */
export const WS_MAX_RECONNECT_INTERVAL = 10000;

/** Base reconnection interval (milliseconds) */
export const WS_BASE_RECONNECT_INTERVAL = 1000;

// Typography
/** Monospace font stack for console/code display */
export const MONOSPACE_FONT = 'Monaco, Consolas, "Liberation Mono", "Courier New", monospace';

/**
 * Build WebSocket URL for a given endpoint
 * Handles both development (proxied) and production environments
 * @param endpoint - The WebSocket endpoint path (e.g., '/ws', '/ws/log')
 * @param token - Optional authentication token to include as query param
 */
export function buildWsUrl(endpoint: string, token?: string): string {
  const protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
  const host = window.location.host;
  const base = `${protocol}//${host}${endpoint}`;
  return token ? `${base}?token=${encodeURIComponent(token)}` : base;
}
