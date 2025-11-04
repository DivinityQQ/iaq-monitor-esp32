/**
 * Application-wide constants
 * Centralizes magic numbers and configuration values for easier maintenance
 */

// Stream Buffer Configuration
/** Ring buffer capacity: 1 hour @ 1 Hz with cushion (3600 + 100) */
export const STREAM_BUFFER_CAPACITY = 3700;

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
