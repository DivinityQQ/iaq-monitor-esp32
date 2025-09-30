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

/* FNV-1a 32-bit helpers */
static inline uint32_t fnv1a32_init(void) { return 2166136261u; }
static inline uint32_t fnv1a32_step(uint32_t h, uint8_t b) { return (h ^ b) * 16777619u; }
static uint32_t hash_u32(uint32_t h, uint32_t v)
{
    for (int i = 0; i < 4; ++i) h = fnv1a32_step(h, (uint8_t)((v >> (i*8)) & 0xFF));
    return h;
}

/* Quantize float with scaling and round-to-nearest; NaN -> sentinel */
static inline int32_t quantize_float(float f, float scale)
{
    if (isnan(f)) return INT32_MIN;
    float x = f * scale;
    if (x >= 0.f) x += 0.5f; else x -= 0.5f;
    long v = (long)x;
    if (v > INT32_MAX) v = INT32_MAX;
    if (v < INT32_MIN) v = INT32_MIN;
    return (int32_t)v;
}

uint32_t iaq_data_fingerprint(const iaq_data_t *data)
{
    if (!data) return 0;
    uint32_t h = fnv1a32_init();
    /* Environmental */
    h = hash_u32(h, (uint32_t)quantize_float(data->temperature,     10.f));
    h = hash_u32(h, (uint32_t)quantize_float(data->mcu_temperature, 10.f));
    h = hash_u32(h, (uint32_t)quantize_float(data->humidity,        10.f));
    h = hash_u32(h, (uint32_t)quantize_float(data->pressure,        10.f));
    /* Air quality */
    h = hash_u32(h, (uint32_t)quantize_float(data->co2_ppm,          1.f));
    h = hash_u32(h, (uint32_t)quantize_float(data->pm1_0,           10.f));
    h = hash_u32(h, (uint32_t)quantize_float(data->pm2_5,           10.f));
    h = hash_u32(h, (uint32_t)quantize_float(data->pm10,            10.f));
    h = hash_u32(h, (uint32_t)(data->voc_index == UINT16_MAX ? 0xFFFFFFFFu : data->voc_index));
    h = hash_u32(h, (uint32_t)(data->nox_index == UINT16_MAX ? 0xFFFFFFFFu : data->nox_index));
    /* Derived */
    h = hash_u32(h, (uint32_t)(data->aqi == UINT16_MAX ? 0xFFFFFFFFu : data->aqi));
    h = hash_u32(h, (uint32_t)data->overall_quality);
    h = hash_u32(h, (uint32_t)(uintptr_t)(data->comfort)); /* pointer stability OK for coarse fp */
    /* Note: do not include updated_at in fingerprint to avoid republishes
       when values quantize to the same output. */
    return h;
}
