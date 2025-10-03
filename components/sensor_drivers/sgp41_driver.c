/* components/sensor_drivers/sgp41_driver.c */
#include "sgp41_driver.h"
#include "i2c_bus.h"
#include "esp_log.h"
#include "sdkconfig.h"

#ifdef CONFIG_IAQ_SIMULATION
#include "sensor_sim.h"
#endif

static const char *TAG = "SGP41_DRIVER";

static bool s_initialized = false;

esp_err_t sgp41_driver_init(void)
{
    if (s_initialized) {
        ESP_LOGW(TAG, "SGP41 driver already initialized");
        return ESP_OK;
    }

    /* I2C bus should already be initialized by sensor_coordinator */
    /* In real implementation, we would probe for device at 0x59 */

    s_initialized = true;
    ESP_LOGI(TAG, "SGP41 VOC/NOx sensor driver initialized");
    return ESP_OK;
}

esp_err_t sgp41_driver_read(uint16_t *out_voc_index, uint16_t *out_nox_index,
                             float temp_c, float humidity_rh)
{
    if (!s_initialized) {
        ESP_LOGE(TAG, "SGP41 driver not initialized");
        return ESP_ERR_INVALID_STATE;
    }

#ifdef CONFIG_IAQ_SIMULATION
    return sensor_sim_read_voc_nox(out_voc_index, out_nox_index);
#else
    /* Stub: real implementation will use I2C and Sensirion's VOC/NOx algorithm */
    /* SGP41 I2C address: 0x59 */
    /* Command sequence:
     * 1. Execute conditioning (0x2612) for first 10s
     * 2. Send measure raw signals (0x2619) with RH and T compensation
     * 3. Wait 50ms for measurement
     * 4. Read 6 bytes (VOC raw, NOx raw with CRC)
     * 5. Apply Sensirion Gas Index Algorithm library
     */

    (void)temp_c;
    (void)humidity_rh;

    if (out_voc_index) *out_voc_index = UINT16_MAX;
    if (out_nox_index) *out_nox_index = UINT16_MAX;

    ESP_LOGW(TAG, "SGP41 read: stub implementation, returning UINT16_MAX");
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

esp_err_t sgp41_driver_reset(void)
{
    if (!s_initialized) {
        ESP_LOGE(TAG, "SGP41 driver not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    /* Stub: real implementation would send soft reset command (0x0006) via I2C */
    ESP_LOGI(TAG, "SGP41 driver reset (stub)");
    return ESP_OK;
}

esp_err_t sgp41_driver_deinit(void)
{
    if (!s_initialized) {
        return ESP_OK;
    }

    s_initialized = false;
    ESP_LOGI(TAG, "SGP41 driver deinitialized");
    return ESP_OK;
}
