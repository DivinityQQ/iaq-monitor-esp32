/**
 * Formatting utilities for health dashboard and system information
 */

/**
 * Format uptime in seconds to human-readable string
 * @param seconds - Uptime in seconds
 * @returns Formatted string like "5d 3h 12m"
 */
export function formatUptime(seconds: number): string {
  if (seconds < 0) return '0m';

  const days = Math.floor(seconds / 86400);
  const hours = Math.floor((seconds % 86400) / 3600);
  const minutes = Math.floor((seconds % 3600) / 60);

  const parts: string[] = [];
  if (days > 0) parts.push(`${days}d`);
  if (hours > 0) parts.push(`${hours}h`);
  if (minutes > 0) parts.push(`${minutes}m`);

  return parts.length > 0 ? parts.join(' ') : '0m';
}

/**
 * Format bytes to human-readable string with appropriate unit
 * @param bytes - Number of bytes
 * @returns Formatted string like "2.45 MB"
 */
export function formatBytes(bytes: number): string {
  if (bytes < 0) return '0 B';
  if (bytes < 1024) return `${bytes} B`;
  if (bytes < 1024 * 1024) return `${(bytes / 1024).toFixed(1)} KB`;
  return `${(bytes / (1024 * 1024)).toFixed(2)} MB`;
}

/**
 * Format Unix epoch timestamp to locale-formatted date/time string
 * @param epoch - Unix timestamp in seconds
 * @returns Formatted date/time string
 */
export function formatEpoch(epoch: number): string {
  if (epoch <= 0) return 'Not synced';

  const date = new Date(epoch * 1000);
  return date.toLocaleString();
}

/**
 * Calculate percentage with optional decimal places
 * @param value - Current value
 * @param total - Total value
 * @param decimals - Number of decimal places (default: 0)
 * @returns Percentage value
 */
export function calculatePercentage(value: number, total: number, decimals: number = 0): number {
  if (total === 0) return 0;
  return Number(((value / total) * 100).toFixed(decimals));
}

/**
 * Format percentage value with symbol
 * @param value - Current value
 * @param total - Total value
 * @param decimals - Number of decimal places (default: 0)
 * @returns Formatted percentage string like "75%"
 */
export function formatPercentage(value: number, total: number, decimals: number = 0): string {
  return `${calculatePercentage(value, total, decimals)}%`;
}
