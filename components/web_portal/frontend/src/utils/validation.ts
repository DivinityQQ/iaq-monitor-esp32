/**
 * Form validation and utility functions
 */

/**
 * Validates WiFi SSID
 * @param ssid - The SSID to validate
 * @returns Error message or null if valid
 */
export function validateSSID(ssid: string): string | null {
  if (!ssid || ssid.trim().length === 0) {
    return 'SSID is required';
  }

  if (ssid.length > 32) {
    return 'SSID must be 32 characters or less';
  }

  return null;
}

/**
 * Validates WiFi password based on authentication mode
 * @param password - The password to validate
 * @param authMode - The authentication mode (e.g., 'WPA2_PSK', 'OPEN')
 * @returns Error message or null if valid
 */
export function validatePassword(password: string, authMode: string = 'WPA2_PSK'): string | null {
  // Open networks don't require passwords
  if (authMode === 'OPEN' || authMode === 'NONE') {
    return null;
  }

  if (!password || password.length === 0) {
    return 'Password is required for secured networks';
  }

  // WPA/WPA2 passwords must be 8-63 characters
  if (password.length < 8) {
    return 'Password must be at least 8 characters';
  }

  if (password.length > 63) {
    return 'Password must be 63 characters or less';
  }

  return null;
}

/**
 * Validates MQTT broker URL
 * @param url - The MQTT broker URL to validate
 * @returns Error message or null if valid
 */
export function validateMQTTUrl(url: string): string | null {
  if (!url || url.trim().length === 0) {
    return 'Broker URL is required';
  }

  // Must start with mqtt:// or mqtts://
  const mqttUrlPattern = /^mqtts?:\/\/.+/i;
  if (!mqttUrlPattern.test(url)) {
    return 'URL must start with mqtt:// or mqtts://';
  }

  // Basic URL structure validation
  try {
    const parsedUrl = new URL(url);
    if (!parsedUrl.hostname) {
      return 'Invalid URL format';
    }
  } catch {
    return 'Invalid URL format';
  }

  return null;
}

/**
 * Formats a Unix timestamp or seconds value to relative time
 * @param seconds - Unix timestamp or seconds ago
 * @param isTimestamp - If true, treats input as Unix timestamp; otherwise as seconds ago
 * @returns Formatted relative time string (e.g., "2s ago", "5m ago")
 */
export function formatRelativeTime(seconds: number, isTimestamp: boolean = false): string {
  let secondsAgo: number;

  if (isTimestamp) {
    // Convert Unix timestamp to seconds ago
    secondsAgo = Math.floor(Date.now() / 1000) - seconds;
  } else {
    // Already in seconds ago format
    secondsAgo = seconds;
  }

  if (secondsAgo < 0) {
    return 'just now';
  }

  if (secondsAgo < 60) {
    return `${secondsAgo}s ago`;
  }

  const minutesAgo = Math.floor(secondsAgo / 60);
  if (minutesAgo < 60) {
    return `${minutesAgo}m ago`;
  }

  const hoursAgo = Math.floor(minutesAgo / 60);
  if (hoursAgo < 24) {
    return `${hoursAgo}h ago`;
  }

  const daysAgo = Math.floor(hoursAgo / 24);
  return `${daysAgo}d ago`;
}

/**
 * Formats milliseconds to human-readable duration
 * @param ms - Milliseconds
 * @returns Formatted duration string (e.g., "1s", "5s", "1m")
 */
export function formatDuration(ms: number): string {
  if (ms === 0) {
    return 'Disabled';
  }

  if (ms < 1000) {
    return `${ms}ms`;
  }

  const seconds = ms / 1000;
  if (seconds < 60) {
    return `${seconds}s`;
  }

  const minutes = Math.floor(seconds / 60);
  const remainingSeconds = seconds % 60;

  if (remainingSeconds === 0) {
    return `${minutes}m`;
  }

  return `${minutes}m ${remainingSeconds}s`;
}

/**
 * Formats seconds to an integer seconds string without decimals.
 * Useful for countdowns like warmup remaining.
 */
export function formatSeconds(seconds: number): string {
  if (!isFinite(seconds) || seconds <= 0) return '0s';
  // Ceil to avoid prematurely showing 0 while still warming up
  return `${Math.ceil(seconds)}s`;
}

/**
 * Signal strength category type
 */
export type SignalStrength = 'excellent' | 'good' | 'fair' | 'weak';

/**
 * Gets WiFi signal strength category from RSSI
 * @param rssi - Received Signal Strength Indicator (dBm)
 * @returns Signal strength category
 */
export function getSignalStrength(rssi: number): SignalStrength {
  if (rssi >= -50) return 'excellent';
  if (rssi >= -60) return 'good';
  if (rssi >= -70) return 'fair';
  return 'weak';
}

/**
 * Gets display label for signal strength category
 * @param strength - Signal strength category
 * @returns Human-readable label
 */
export function getSignalStrengthLabel(strength: SignalStrength): string {
  const labels: Record<SignalStrength, string> = {
    excellent: 'Excellent',
    good: 'Good',
    fair: 'Fair',
    weak: 'Poor',
  };
  return labels[strength];
}

/**
 * Gets MUI chip color for signal strength category
 * @param strength - Signal strength category
 * @returns MUI chip color
 */
export function getSignalStrengthColor(strength: SignalStrength): 'success' | 'warning' | 'error' {
  if (strength === 'excellent' || strength === 'good') return 'success';
  if (strength === 'fair') return 'warning';
  return 'error';
}

/**
 * Gets signal strength bars (1-4) from RSSI
 * @param rssi - Received Signal Strength Indicator (dBm)
 * @returns Number of signal bars (1-4)
 */
export function getSignalBars(rssi: number): number {
  if (rssi >= -50) return 4; // Excellent
  if (rssi >= -60) return 3; // Good
  if (rssi >= -70) return 2; // Fair
  return 1; // Weak
}
