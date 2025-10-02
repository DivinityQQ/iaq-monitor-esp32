/* components/iaq_data/iaq_data.c */
#include "iaq_data.h"
#include <math.h>
#include "esp_log.h"
#include <string.h>
#include <limits.h>

static const char *TAG = "IAQ_DATA";

/* Global instance */
static iaq_data_t g_iaq_data = {0};

esp_err_t iaq_data_init(void)
{
    ESP_LOGI(TAG, "Initializing IAQ data structure");

    /* Initialize with default values - memset FIRST before creating mutex */
    memset(&g_iaq_data, 0, sizeof(iaq_data_t));

    /* Create mutex AFTER memset */
    g_iaq_data.mutex = xSemaphoreCreateMutex();
    if (g_iaq_data.mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create mutex");
        return ESP_FAIL;
    }

    /* Mark no-data for all numeric fields */
    g_iaq_data.temperature = NAN;
    g_iaq_data.humidity = NAN;
    g_iaq_data.pressure = NAN;
    g_iaq_data.pressure_trend = NAN;
    g_iaq_data.co2_ppm = NAN;
    g_iaq_data.pm1_0 = NAN;
    g_iaq_data.pm2_5 = NAN;
    g_iaq_data.pm10 = NAN;
    g_iaq_data.voc_index = UINT16_MAX;
    g_iaq_data.nox_index = UINT16_MAX;
    g_iaq_data.aqi = UINT16_MAX;
    g_iaq_data.overall_quality = 0xFF;
    g_iaq_data.mcu_temperature = NAN;

    g_iaq_data.warming_up = true;
    g_iaq_data.comfort = "unknown";

    /* Initialize all validity flags to false */
    g_iaq_data.valid.temperature = false;
    g_iaq_data.valid.mcu_temperature = false;
    g_iaq_data.valid.humidity = false;
    g_iaq_data.valid.pressure = false;
    g_iaq_data.valid.co2_ppm = false;
    g_iaq_data.valid.pm1_0 = false;
    g_iaq_data.valid.pm2_5 = false;
    g_iaq_data.valid.pm10 = false;
    g_iaq_data.valid.voc_index = false;
    g_iaq_data.valid.nox_index = false;

    /* Timestamps already zeroed by memset */

    ESP_LOGI(TAG, "IAQ data structure initialized");
    return ESP_OK;
}

iaq_data_t* iaq_data_get(void)
{
    return &g_iaq_data;
}

bool iaq_data_lock(uint32_t timeout_ms)
{
    if (g_iaq_data.mutex == NULL) {
        ESP_LOGE(TAG, "Mutex not initialized");
        return false;
    }

    TickType_t timeout_ticks = (timeout_ms == portMAX_DELAY) ?
                                portMAX_DELAY :
                                pdMS_TO_TICKS(timeout_ms);

    return xSemaphoreTake(g_iaq_data.mutex, timeout_ticks) == pdTRUE;
}

void iaq_data_unlock(void)
{
    if (g_iaq_data.mutex == NULL) {
        ESP_LOGE(TAG, "Mutex not initialized");
        return;
    }

    xSemaphoreGive(g_iaq_data.mutex);
}
