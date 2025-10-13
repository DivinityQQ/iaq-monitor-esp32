/* components/display_oled/include/display_oled/display_util.h */
#ifndef DISPLAY_UTIL_H
#define DISPLAY_UTIL_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Format floating-point value with fixed decimals using integer arithmetic.
 * Avoids float printf bloat by multiplying, rounding, and formatting as int.
 *
 * @param buf Output buffer
 * @param len Buffer size
 * @param val Float value to format
 * @param decimals Number of decimal places (0-2 supported)
 * @param fallback String to use if value is NaN or infinite
 *
 * Examples:
 *   fmt_float(buf, 16, 22.14, 1, "---") → "22.1"
 *   fmt_float(buf, 16, 850.0, 0, "---") → "850"
 *   fmt_float(buf, 16, NAN, 1, "n/a") → "n/a"
 */
void fmt_float(char *buf, size_t len, float val, int decimals, const char *fallback);

/**
 * Format integer value.
 *
 * @param buf Output buffer
 * @param len Buffer size
 * @param val Integer value
 * @param fallback String to use if value is invalid (optional, can be NULL)
 */
void fmt_int(char *buf, size_t len, int val, const char *fallback);

/**
 * Format uptime in human-readable form: "2d 14h 32m"
 *
 * @param buf Output buffer
 * @param len Buffer size
 * @param seconds Total uptime in seconds
 */
void fmt_uptime(char *buf, size_t len, uint32_t seconds);

/**
 * Get short AQI category string.
 *
 * @param aqi AQI value (0-500)
 * @return Short category: "Good", "Mod", "USG", "Unhlt", "V.Unhl", "Hazrd"
 */
const char* get_aqi_short(uint16_t aqi);

/**
 * Get arrow icon for pressure trend.
 *
 * @param trend Pressure trend enum value
 * @return Pointer to icon data (8 bytes)
 */
const uint8_t* get_pressure_trend_icon(int trend);

/**
 * Check if any sensor is currently warming up.
 * Uses cached result to reduce coordinator calls (updates every 1000ms).
 *
 * @return true if at least one sensor is in WARMING state
 */
bool any_sensor_warming(void);

/**
 * Get warming progress for the sensor with the longest remaining warmup time.
 * Calculates percentage based on elapsed vs. total warmup time.
 *
 * @return Progress percentage (0-100), 100 if no sensors warming
 */
uint8_t get_warming_progress(void);

/**
 * Get overall sensor system status text.
 * Uses cached result (updates every 1000ms).
 * Priority: ERROR > WARMING > INIT > UNINIT, defaults to READY if all ready.
 *
 * @return Status string: "ERROR", "WARMING", "INIT", "UNINIT", or "READY"
 */
const char* get_sensor_status_text(void);

#ifdef __cplusplus
}
#endif

#endif /* DISPLAY_UTIL_H */
