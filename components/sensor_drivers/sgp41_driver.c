/* components/sensor_drivers/sgp41_driver.c */
#include "sgp41_driver.h"
#include "i2c_bus.h"
#include "esp_log.h"
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"

#include "sensirion_gas_index_algorithm.h"
#include "nvs_flash.h"
#include "nvs.h"

#ifdef CONFIG_IAQ_SIMULATION
#include "sensor_sim.h"
#endif

static const char *TAG = "SGP41_DRIVER";

static bool s_initialized = false;
static i2c_master_dev_handle_t s_dev = NULL;
static GasIndexAlgorithmParams s_voc_params;
static GasIndexAlgorithmParams s_nox_params;
static int64_t s_init_time_us = 0;
static uint8_t s_cond_err_streak = 0;
static bool s_cond_warned = false;
static bool s_voc_state_restored = false;
static bool s_voc_state_saved = false;

#define SGP41_I2C_ADDR 0x59
#define SGP41_CMD_EXECUTE_CONDITIONING 0x2612
#define SGP41_CMD_MEASURE_RAW_SIGNALS  0x2619
#define SGP41_MEAS_DELAY_MS 60
#define NVS_NAMESPACE_SGP41 "sgp41_algo"

#define SENSIRION_CRC_POLY 0x31
#define SENSIRION_CRC_INIT 0xFF

static uint8_t sensirion_crc8(const uint8_t *data, size_t len) {
    uint8_t crc = SENSIRION_CRC_INIT;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int b = 0; b < 8; b++) {
            if (crc & 0x80) crc = (uint8_t)((crc << 1) ^ SENSIRION_CRC_POLY);
            else crc <<= 1;
        }
    }
    return crc;
}

static void build_cmd_args(uint16_t cmd, uint16_t rh_ticks, uint16_t t_ticks, uint8_t tx[8])
{
    tx[0] = (uint8_t)(cmd >> 8);
    tx[1] = (uint8_t)(cmd & 0xFF);
    tx[2] = (uint8_t)(rh_ticks >> 8);
    tx[3] = (uint8_t)(rh_ticks & 0xFF);
    tx[4] = sensirion_crc8(&tx[2], 2);
    tx[5] = (uint8_t)(t_ticks >> 8);
    tx[6] = (uint8_t)(t_ticks & 0xFF);
    tx[7] = sensirion_crc8(&tx[5], 2);
}

static inline bool verify_rx_word(uint8_t hi, uint8_t lo, uint8_t crc)
{
    uint8_t tmp[2] = {hi, lo};
    return sensirion_crc8(tmp, 2) == crc;
}

static void voc_try_restore_state_from_nvs(void)
{
    if (s_voc_state_restored) return;
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE_SGP41, NVS_READONLY, &h) != ESP_OK) return;
    struct { float s0; float s1; } st;
    size_t sz = sizeof(st);
    esp_err_t err = nvs_get_blob(h, "voc_states", &st, &sz);
    nvs_close(h);
    if (err == ESP_OK && sz == sizeof(st)) {
        GasIndexAlgorithm_set_states(&s_voc_params, st.s0, st.s1);
        s_voc_state_restored = true;
        ESP_LOGI(TAG, "SGP41 VOC algorithm states restored from NVS");
    }
}

static void voc_try_persist_state_to_nvs(void)
{
    if (s_voc_state_saved) return;
    const int64_t three_hours_us = 3LL * 3600LL * 1000000LL;
    if ((esp_timer_get_time() - s_init_time_us) < three_hours_us) return;
    float s0 = 0.0f, s1 = 0.0f;
    GasIndexAlgorithm_get_states(&s_voc_params, &s0, &s1);
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE_SGP41, NVS_READWRITE, &h) != ESP_OK) return;
    struct { float s0; float s1; } st = {s0, s1};
    (void)nvs_set_blob(h, "voc_states", &st, sizeof(st));
    (void)nvs_commit(h);
    nvs_close(h);
    s_voc_state_saved = true;
    ESP_LOGI(TAG, "SGP41 VOC algorithm states saved to NVS");
}

static inline uint16_t sgp41_rh_to_ticks(float rh_percent) {
    if (rh_percent < 0.0f) rh_percent = 0.0f;
    if (rh_percent > 100.0f) rh_percent = 100.0f;
    return (uint16_t)((rh_percent * 65535.0f) / 100.0f + 0.5f);
}

static inline uint16_t sgp41_t_to_ticks(float temp_c) {
    if (temp_c < -45.0f) temp_c = -45.0f;
    if (temp_c > 130.0f) temp_c = 130.0f;
    return (uint16_t)(((temp_c + 45.0f) * 65535.0f) / 175.0f + 0.5f);
}

esp_err_t sgp41_driver_init(void)
{
    if (s_initialized) {
        ESP_LOGW(TAG, "SGP41 driver already initialized");
        return ESP_OK;
    }

    /* I2C bus should already be initialized by sensor_coordinator */
    esp_err_t ret = i2c_bus_add_device(SGP41_I2C_ADDR, CONFIG_IAQ_I2C_FREQ_HZ, &s_dev);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add SGP41 device: %s", esp_err_to_name(ret));
        return ret;
    }

    /* Initialize gas index algorithms (sampling interval: cadence or 1s default) */
    float sample_s = (CONFIG_IAQ_CADENCE_SGP41_MS > 0) ? (CONFIG_IAQ_CADENCE_SGP41_MS / 1000.0f) : 1.0f;
    GasIndexAlgorithm_init_with_sampling_interval(&s_voc_params, GasIndexAlgorithm_ALGORITHM_TYPE_VOC, sample_s);
    GasIndexAlgorithm_init_with_sampling_interval(&s_nox_params, GasIndexAlgorithm_ALGORITHM_TYPE_NOX, sample_s);

    s_initialized = true;
    s_init_time_us = esp_timer_get_time();
    s_cond_err_streak = 0;
    s_cond_warned = false;
    s_voc_state_saved = false;
    voc_try_restore_state_from_nvs();
    ESP_LOGI(TAG, "SGP41 VOC/NOx sensor driver initialized");
    return ESP_OK;
}

bool sgp41_driver_is_reporting_ready(void)
{
    if (!s_initialized || s_dev == NULL) return false;
    const int32_t cond_ms = (CONFIG_IAQ_WARMUP_SGP41_MS > 10000) ? 10000 : CONFIG_IAQ_WARMUP_SGP41_MS;
    const int32_t blackout_ms = (int32_t)(GasIndexAlgorithm_INITIAL_BLACKOUT * 1000.0f + 0.5f); // ~45000ms
    const int64_t elapsed_us = esp_timer_get_time() - s_init_time_us;
    return elapsed_us >= ((int64_t)cond_ms + blackout_ms) * 1000LL;
}

esp_err_t sgp41_driver_conditioning_tick(float temp_c, float humidity_rh)
{
    if (!s_initialized || s_dev == NULL) return ESP_ERR_INVALID_STATE;

    const int32_t clamp_ms = (CONFIG_IAQ_WARMUP_SGP41_MS > 10000) ? 10000 : CONFIG_IAQ_WARMUP_SGP41_MS;
    int64_t since_init_us = esp_timer_get_time() - s_init_time_us;
    if (since_init_us >= (int64_t)clamp_ms * 1000LL) {
        return ESP_OK; // conditioning window elapsed
    }

    uint16_t rh_ticks = sgp41_rh_to_ticks(humidity_rh);
    uint16_t t_ticks  = sgp41_t_to_ticks(temp_c);
    uint8_t tx[8];
    build_cmd_args(SGP41_CMD_EXECUTE_CONDITIONING, rh_ticks, t_ticks, tx);
    esp_err_t ret = i2c_master_transmit(s_dev, tx, sizeof(tx), CONFIG_IAQ_I2C_TIMEOUT_MS);
    if (ret != ESP_OK) return ret;
    vTaskDelay(pdMS_TO_TICKS(SGP41_MEAS_DELAY_MS));
    /* Read 3 bytes: VOC[2] + CRC. Ignore value but verify CRC for health */
    uint8_t rx[3] = {0};
    ret = i2c_master_receive(s_dev, rx, sizeof(rx), CONFIG_IAQ_I2C_TIMEOUT_MS);
    if (ret == ESP_OK && !verify_rx_word(rx[0], rx[1], rx[2])) {
        ret = ESP_ERR_INVALID_CRC;
    }
    if (ret != ESP_OK) {
        s_cond_err_streak++;
        if (s_cond_err_streak >= 3 && !s_cond_warned) {
            ESP_LOGW(TAG, "SGP41 conditioning: %u consecutive errors (CRC/timeout)", (unsigned)s_cond_err_streak);
            s_cond_warned = true;
        }
    } else {
        s_cond_err_streak = 0;
    }
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
    if (s_dev == NULL) {
        ESP_LOGE(TAG, "SGP41 HAL not ready");
        return ESP_ERR_INVALID_STATE;
    }

    // During the first <=10 seconds after init, run execute_conditioning to prime NOx baseline.
    const int32_t clamp_ms = (CONFIG_IAQ_WARMUP_SGP41_MS > 10000) ? 10000 : CONFIG_IAQ_WARMUP_SGP41_MS;
    int64_t since_init_us = esp_timer_get_time() - s_init_time_us;
    uint16_t rh_ticks = sgp41_rh_to_ticks(humidity_rh);
    uint16_t t_ticks  = sgp41_t_to_ticks(temp_c);
    if (since_init_us < (int64_t)clamp_ms * 1000LL) {
        // Run conditioning using the helper and signal skip
        (void)sgp41_driver_conditioning_tick(temp_c, humidity_rh);
        return ESP_ERR_NOT_SUPPORTED;
    }

    /* Build measure command */
    uint8_t tx[8];
    build_cmd_args(SGP41_CMD_MEASURE_RAW_SIGNALS, rh_ticks, t_ticks, tx);
    esp_err_t ret = i2c_master_transmit(s_dev, tx, sizeof(tx), CONFIG_IAQ_I2C_TIMEOUT_MS);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "SGP41 transmit failed: %s", esp_err_to_name(ret));
        // One-shot retry on transient error
        ret = i2c_master_transmit(s_dev, tx, sizeof(tx), CONFIG_IAQ_I2C_TIMEOUT_MS);
        if (ret != ESP_OK) return ret;
    }
    vTaskDelay(pdMS_TO_TICKS(SGP41_MEAS_DELAY_MS));
    /* Read 6 bytes: VOC[2] CRC, NOx[2] CRC */
    uint8_t rx[6] = {0};
    ret = i2c_master_receive(s_dev, rx, sizeof(rx), CONFIG_IAQ_I2C_TIMEOUT_MS);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "SGP41 receive failed: %s", esp_err_to_name(ret));
        // One-shot retry
        ret = i2c_master_receive(s_dev, rx, sizeof(rx), CONFIG_IAQ_I2C_TIMEOUT_MS);
        if (ret != ESP_OK) return ret;
    }
    if (!verify_rx_word(rx[0], rx[1], rx[2]) || !verify_rx_word(rx[3], rx[4], rx[5])) {
        ESP_LOGW(TAG, "SGP41 CRC check failed");
        return ESP_ERR_INVALID_CRC;
    }
    uint16_t sraw_voc = ((uint16_t)rx[0] << 8) | rx[1];
    uint16_t sraw_nox = ((uint16_t)rx[3] << 8) | rx[4];

    /* Process gas index (map to 0..500). */
    int32_t voc_index = 0, nox_index = 0;
    GasIndexAlgorithm_process(&s_voc_params, (int32_t)sraw_voc, &voc_index);
    GasIndexAlgorithm_process(&s_nox_params, (int32_t)sraw_nox, &nox_index);

    if (out_voc_index) *out_voc_index = (uint16_t)voc_index;
    if (out_nox_index) *out_nox_index = (uint16_t)nox_index;
    voc_try_persist_state_to_nvs();

    return ESP_OK;
#endif
}

esp_err_t sgp41_driver_reset(void)
{
    if (!s_initialized) {
        ESP_LOGE(TAG, "SGP41 driver not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (s_dev == NULL) return ESP_ERR_INVALID_STATE;
    // Algorithm logical reset (sensor itself has no dedicated soft reset command here)
    float sample_s = (CONFIG_IAQ_CADENCE_SGP41_MS > 0) ? (CONFIG_IAQ_CADENCE_SGP41_MS / 1000.0f) : 1.0f;
    GasIndexAlgorithm_init_with_sampling_interval(&s_voc_params, GasIndexAlgorithm_ALGORITHM_TYPE_VOC, sample_s);
    GasIndexAlgorithm_init_with_sampling_interval(&s_nox_params, GasIndexAlgorithm_ALGORITHM_TYPE_NOX, sample_s);
    s_init_time_us = esp_timer_get_time();
    ESP_LOGI(TAG, "SGP41 algorithm state reset");
    return ESP_OK;
}

esp_err_t sgp41_driver_deinit(void)
{
    if (!s_initialized) {
        return ESP_OK;
    }

    if (s_dev) {
        (void)i2c_master_bus_rm_device(s_dev);
        s_dev = NULL;
    }
    s_initialized = false;
    ESP_LOGI(TAG, "SGP41 driver deinitialized");
    return ESP_OK;
}
