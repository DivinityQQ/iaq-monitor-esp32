/* components/iaq_data/include/iaq_data.h */
#ifndef IAQ_DATA_H
#define IAQ_DATA_H

#include <stdint.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

/**
 * Global data structure for all IAQ measurements and system state.
 * Access must be protected by mutex.
 */
typedef struct {
    SemaphoreHandle_t mutex;

    /* Environmental baseline */
    float temperature;          // °C
    float mcu_temperature;      // °C (internal MCU sensor)
    float humidity;             // %RH
    float pressure;             // hPa
    float pressure_trend;       // hPa/hour (3-hour trend)

    /* Air quality measurements */
    float co2_ppm;              // CO₂ concentration
    float pm1_0;                // PM1.0 µg/m³
    float pm2_5;                // PM2.5 µg/m³
    float pm10;                 // PM10 µg/m³
    uint16_t voc_index;         // VOC index (0-500)
    uint16_t nox_index;         // NOx index (0-500)

    /* Derived metrics */
    uint16_t aqi;               // Air Quality Index
    const char *comfort;        // Comfort level string

    /* Metadata */
    uint32_t last_update;       // Timestamp of last update (seconds since boot)
    uint8_t overall_quality;    // Overall air quality (0-100)
    bool warming_up;            // True if sensors still warming up

    /* Sensor health status */
    struct {
        bool sht41_ok;
        bool bmp280_ok;
        bool sgp41_ok;
        bool pms5003_ok;
        bool s8_ok;
        uint32_t last_error_code;
    } health;

    /* System status */
    struct {
        bool wifi_connected;
        bool mqtt_connected;
        int32_t wifi_rssi;
        uint32_t uptime_seconds;
        uint32_t free_heap;
        uint32_t min_free_heap;
    } system;

} iaq_data_t;

/**
 * Initialize the global data structure.
 * Must be called before any other iaq_data functions.
 *
 * @return ESP_OK on success, ESP_FAIL if mutex creation fails
 */
esp_err_t iaq_data_init(void);

/**
 * Get a pointer to the global data structure.
 * WARNING: You must call iaq_data_lock() before reading/writing
 * and iaq_data_unlock() when done.
 *
 * @return Pointer to global data
 */
iaq_data_t* iaq_data_get(void);

/**
 * Lock the data structure for exclusive access.
 *
 * @param timeout_ms Timeout in milliseconds (portMAX_DELAY for infinite)
 * @return true if lock acquired, false on timeout
 */
bool iaq_data_lock(uint32_t timeout_ms);

/**
 * Unlock the data structure.
 */
void iaq_data_unlock(void);

/**
 * Scoped lock helper that guarantees unlock even on early returns.
 * Usage:
 *   IAQ_DATA_WITH_LOCK() {
 *       iaq_data_t *data = iaq_data_get();
 *       // ... read/write data
 *   }
 */
#define IAQ_DATA_WITH_LOCK() \
    for (int _iaq_lock_once = iaq_data_lock(portMAX_DELAY); _iaq_lock_once; _iaq_lock_once = (iaq_data_unlock(), 0))

#endif /* IAQ_DATA_H */
