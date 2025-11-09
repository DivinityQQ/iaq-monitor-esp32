/* components/sensor_drivers/bmp280_driver.c */
#include "bmp280_driver.h"
#include "i2c_bus.h"
#include "esp_log.h"
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <math.h>
#include <string.h>
#include <inttypes.h>

#ifdef CONFIG_IAQ_SIMULATION
#include "sensor_sim.h"
#endif

static const char *TAG = "BMP280_DRIVER";

/* BMP280 registers */
#define BMP280_REG_DIG_T1      0x88
#define BMP280_REG_DIG_T2      0x8A
#define BMP280_REG_DIG_T3      0x8C
#define BMP280_REG_DIG_P1      0x8E
#define BMP280_REG_DIG_P2      0x90
#define BMP280_REG_DIG_P3      0x92
#define BMP280_REG_DIG_P4      0x94
#define BMP280_REG_DIG_P5      0x96
#define BMP280_REG_DIG_P6      0x98
#define BMP280_REG_DIG_P7      0x9A
#define BMP280_REG_DIG_P8      0x9C
#define BMP280_REG_DIG_P9      0x9E

#define BMP280_REG_CHIPID      0xD0
#define BMP280_REG_RESET       0xE0
#define BMP280_REG_STATUS      0xF3
#define BMP280_REG_CTRL_MEAS   0xF4
#define BMP280_REG_CONFIG      0xF5
#define BMP280_REG_PRESS_MSB   0xF7

#define BMP280_RESET_VALUE     0xB6

/* Status register bits */
#define BMP280_STATUS_MEASURING 0x08

/* ctrl_meas field helpers */
#define BMP280_OSRS_T_SHIFT    5
#define BMP280_OSRS_P_SHIFT    2
#define BMP280_MODE_SLEEP      0x00
#define BMP280_MODE_FORCED     0x01

/* config field helpers */
#define BMP280_FILTER_SHIFT    2
/* t_sb kept at 0 (0.5ms), SPI3W disabled */

typedef struct {
    uint16_t dig_T1;
    int16_t  dig_T2;
    int16_t  dig_T3;
    uint16_t dig_P1;
    int16_t  dig_P2;
    int16_t  dig_P3;
    int16_t  dig_P4;
    int16_t  dig_P5;
    int16_t  dig_P6;
    int16_t  dig_P7;
    int16_t  dig_P8;
    int16_t  dig_P9;
    int32_t  t_fine;
} bmp280_calib_t;

static bool s_initialized = false;
static i2c_master_dev_handle_t s_dev = NULL;
static uint8_t s_addr = 0;
static bmp280_calib_t s_calib;

/* Utility: decode oversampling code to numeric factor */
static inline int osrs_code_to_factor(int code)
{
    switch (code & 0x07) {
        case 0: return 0;  /* skipped */
        case 1: return 1;
        case 2: return 2;
        case 3: return 4;
        case 4: return 8;
        case 5: return 16;
        default: return 1; /* clamp */
    }
}

static inline esp_err_t i2c_read_regs(uint8_t reg, uint8_t *buf, size_t len)
{
    return i2c_bus_read_regs(s_dev, reg, buf, len, CONFIG_IAQ_I2C_TIMEOUT_MS);
}

static esp_err_t i2c_write_reg8(uint8_t reg, uint8_t val)
{
    uint8_t tx[2] = { reg, val };
    return i2c_master_transmit(s_dev, tx, sizeof(tx), CONFIG_IAQ_I2C_TIMEOUT_MS);
}

static esp_err_t bmp280_read_chip_id(uint8_t *out_id)
{
    uint8_t id = 0;
    esp_err_t ret = i2c_read_regs(BMP280_REG_CHIPID, &id, 1);
    if (ret == ESP_OK && out_id) *out_id = id;
    return ret;
}

static esp_err_t bmp280_read_calibration(void)
{
    uint8_t raw[24] = {0};
    esp_err_t ret = i2c_read_regs(BMP280_REG_DIG_T1, raw, sizeof(raw));
    if (ret != ESP_OK) return ret;
    s_calib.dig_T1 = (uint16_t)(raw[1] << 8 | raw[0]);
    s_calib.dig_T2 = (int16_t)(raw[3] << 8 | raw[2]);
    s_calib.dig_T3 = (int16_t)(raw[5] << 8 | raw[4]);
    s_calib.dig_P1 = (uint16_t)(raw[7] << 8 | raw[6]);
    s_calib.dig_P2 = (int16_t)(raw[9] << 8 | raw[8]);
    s_calib.dig_P3 = (int16_t)(raw[11] << 8 | raw[10]);
    s_calib.dig_P4 = (int16_t)(raw[13] << 8 | raw[12]);
    s_calib.dig_P5 = (int16_t)(raw[15] << 8 | raw[14]);
    s_calib.dig_P6 = (int16_t)(raw[17] << 8 | raw[16]);
    s_calib.dig_P7 = (int16_t)(raw[19] << 8 | raw[18]);
    s_calib.dig_P8 = (int16_t)(raw[21] << 8 | raw[20]);
    s_calib.dig_P9 = (int16_t)(raw[23] << 8 | raw[22]);
    return ESP_OK;
}

static esp_err_t bmp280_configure(void)
{
    /* Clamp unsupported values */
    int osrs_t = CONFIG_IAQ_BMP280_OSRS_T;
    int osrs_p = CONFIG_IAQ_BMP280_OSRS_P;
    if (osrs_t < 0 || osrs_t > 5) osrs_t = 1;
    if (osrs_p < 0 || osrs_p > 5) osrs_p = 3;
    if (osrs_t == 0) {
        /* Temperature skipped would make t_fine stale; force to x1 */
        ESP_LOGW(TAG, "osrs_t=0 (skip) is not recommended; overriding to x1");
        osrs_t = 1;
    }
    int filter = CONFIG_IAQ_BMP280_FILTER;
    if (filter < 0 || filter > 4) filter = 2;

    uint8_t cfg = (uint8_t)((0 << 5) | ((filter & 0x07) << BMP280_FILTER_SHIFT) | 0);
    esp_err_t ret = i2c_write_reg8(BMP280_REG_CONFIG, cfg);
    if (ret != ESP_OK) return ret;

    /* Put device into sleep with desired oversampling; reads will set forced */
    uint8_t ctrl_meas = (uint8_t)(((osrs_t & 0x07) << BMP280_OSRS_T_SHIFT) |
                                  ((osrs_p & 0x07) << BMP280_OSRS_P_SHIFT) |
                                  BMP280_MODE_SLEEP);
    ret = i2c_write_reg8(BMP280_REG_CTRL_MEAS, ctrl_meas);
    return ret;
}

static esp_err_t bmp280_soft_reset(void)
{
    esp_err_t ret = i2c_write_reg8(BMP280_REG_RESET, BMP280_RESET_VALUE);
    if (ret != ESP_OK) return ret;
    /* Datasheet: typical 2ms; add generous margin for stability */
    vTaskDelay(pdMS_TO_TICKS(10));
    return ESP_OK;
}

static esp_err_t try_open_at_addr(uint8_t addr, i2c_master_dev_handle_t *out)
{
    i2c_master_dev_handle_t dev = NULL;
    esp_err_t ret = i2c_bus_add_device(addr, CONFIG_IAQ_I2C_FREQ_HZ, &dev);
    if (ret != ESP_OK) return ret;
    /* Temporarily set s_dev to use the helpers */
    i2c_master_dev_handle_t prev = s_dev;
    s_dev = dev;
    uint8_t id = 0;
    ret = bmp280_read_chip_id(&id);
    s_dev = prev;
    if (ret != ESP_OK) {
        (void)i2c_master_bus_rm_device(dev);
        return ret;
    }
    if (id != 0x58 && id != 0x56 && id != 0x57) {
        /* Not a BMP280; reject (BME280=0x60) */
        (void)i2c_master_bus_rm_device(dev);
        return ESP_FAIL;
    }
    *out = dev;
    return ESP_OK;
}

esp_err_t bmp280_driver_init(void)
{
    if (s_initialized) {
        ESP_LOGW(TAG, "BMP280 driver already initialized");
        return ESP_OK;
    }

    /* I2C bus should already be initialized by sensor_coordinator */
    uint8_t addr = 0;
    i2c_master_dev_handle_t dev = NULL;

    if (CONFIG_IAQ_BMP280_ADDR >= 0) {
        addr = (uint8_t)CONFIG_IAQ_BMP280_ADDR;
        esp_err_t ret = try_open_at_addr(addr, &dev);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "BMP280 not found at 0x%02X: %s", addr, esp_err_to_name(ret));
            return ret;
        }
    } else {
        /* Auto probe: 0x76 then 0x77 */
        esp_err_t ret = try_open_at_addr(0x76, &dev);
        if (ret == ESP_OK) {
            addr = 0x76;
        } else {
            ret = try_open_at_addr(0x77, &dev);
            if (ret == ESP_OK) addr = 0x77;
            else {
                ESP_LOGE(TAG, "BMP280 auto-probe failed: not found at 0x76/0x77");
                return ret;
            }
        }
    }

    s_dev = dev;
    s_addr = addr;

    /* Soft reset, read calibration and configure */
    esp_err_t ret = bmp280_soft_reset();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "BMP280 reset failed: %s", esp_err_to_name(ret));
        (void)i2c_master_bus_rm_device(s_dev);
        s_dev = NULL;
        return ret;
    }

    /* Read calibration with a brief retry if values look invalid */
    ret = bmp280_read_calibration();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "BMP280 read calib failed: %s", esp_err_to_name(ret));
        (void)i2c_master_bus_rm_device(s_dev);
        s_dev = NULL;
        return ret;
    }
    if (s_calib.dig_P1 == 0) {
        /* Retry once after extra delay; some parts need more time post-reset */
        vTaskDelay(pdMS_TO_TICKS(10));
        ret = bmp280_read_calibration();
        if (ret != ESP_OK || s_calib.dig_P1 == 0) {
            ESP_LOGE(TAG, "BMP280 invalid calib (dig_P1=%u)", (unsigned)s_calib.dig_P1);
            (void)i2c_master_bus_rm_device(s_dev);
            s_dev = NULL;
            return (ret == ESP_OK) ? ESP_FAIL : ret;
        }
    }

    ret = bmp280_configure();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "BMP280 configure failed: %s", esp_err_to_name(ret));
        (void)i2c_master_bus_rm_device(s_dev);
        s_dev = NULL;
        return ret;
    }

    s_initialized = true;
    ESP_LOGI(TAG, "BMP280 initialized at 0x%02X (osrs_t=%d, osrs_p=%d, filter=%d)",
             s_addr, CONFIG_IAQ_BMP280_OSRS_T, CONFIG_IAQ_BMP280_OSRS_P, CONFIG_IAQ_BMP280_FILTER);
    return ESP_OK;
}

static esp_err_t bmp280_trigger_forced_and_wait(int expected_ms)
{
    /* Re-write ctrl_meas with FORCED mode while keeping osrs bits */
    uint8_t ctrl = 0;
    esp_err_t ret = i2c_read_regs(BMP280_REG_CTRL_MEAS, &ctrl, 1);
    if (ret != ESP_OK) return ret;
    ctrl = (uint8_t)((ctrl & 0xFC) | BMP280_MODE_FORCED);
    ret = i2c_write_reg8(BMP280_REG_CTRL_MEAS, ctrl);
    if (ret != ESP_OK) return ret;

    int timeout_ms = expected_ms + CONFIG_IAQ_BMP280_MEAS_DELAY_MARGIN_MS;
    if (timeout_ms < 5) timeout_ms = 5;
    const TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(timeout_ms);
    bool timed_out = false;
    /* Poll measuring bit with small sleeps */
    while (1) {
        uint8_t status = 0;
        ret = i2c_read_regs(BMP280_REG_STATUS, &status, 1);
        if (ret != ESP_OK) return ret;
        if ((status & BMP280_STATUS_MEASURING) == 0) break;
        if (xTaskGetTickCount() >= deadline) { timed_out = true; break; }
        vTaskDelay(1);
    }
    return timed_out ? ESP_ERR_TIMEOUT : ESP_OK;
}

static float bmp280_compensate_temperature_c(int32_t adc_T)
{
    int32_t var1, var2;
    var1 = ((((adc_T >> 3) - ((int32_t)s_calib.dig_T1 << 1))) * ((int32_t)s_calib.dig_T2)) >> 11;
    var2 = (((((adc_T >> 4) - ((int32_t)s_calib.dig_T1)) * ((adc_T >> 4) - ((int32_t)s_calib.dig_T1))) >> 12) * ((int32_t)s_calib.dig_T3)) >> 14;
    s_calib.t_fine = var1 + var2;
    float T = (s_calib.t_fine * 5 + 128) >> 8;
    return T / 100.0f;
}

static float bmp280_compensate_pressure_pa(int32_t adc_P)
{
    int64_t var1, var2, p;
    var1 = ((int64_t)s_calib.t_fine) - 128000;
    var2 = var1 * var1 * (int64_t)s_calib.dig_P6;
    var2 = var2 + ((var1 * (int64_t)s_calib.dig_P5) << 17);
    var2 = var2 + (((int64_t)s_calib.dig_P4) << 35);
    var1 = ((var1 * var1 * (int64_t)s_calib.dig_P3) >> 8) + ((var1 * (int64_t)s_calib.dig_P2) << 12);
    var1 = (((((int64_t)1) << 47) + var1)) * ((int64_t)s_calib.dig_P1) >> 33;
    if (var1 == 0) {
        return NAN; // avoid division by zero
    }
    p = 1048576 - adc_P;
    p = (((p << 31) - var2) * 3125) / var1;
    var1 = (((int64_t)s_calib.dig_P9) * (p >> 13) * (p >> 13)) >> 25;
    var2 = (((int64_t)s_calib.dig_P8) * p) >> 19;
    p = ((p + var1 + var2) >> 8) + (((int64_t)s_calib.dig_P7) << 4);
    return (float)p / 256.0f; // Pa
}

esp_err_t bmp280_driver_read(float *out_pressure_hpa, float *out_temp_c)
{
    if (!s_initialized || s_dev == NULL) {
        ESP_LOGE(TAG, "BMP280 driver not initialized");
        return ESP_ERR_INVALID_STATE;
    }

#ifdef CONFIG_IAQ_SIMULATION
    esp_err_t ret = ESP_OK;
    if (out_pressure_hpa) {
        ret = sensor_sim_read_pressure(out_pressure_hpa);
        if (ret != ESP_OK) return ret;
    }
    if (out_temp_c) {
        ret = sensor_sim_read_temperature(out_temp_c);
    }
    return ret;
#else
    /* Compute expected measurement time (ms) */
    int osrs_t = CONFIG_IAQ_BMP280_OSRS_T;
    int osrs_p = CONFIG_IAQ_BMP280_OSRS_P;
    int f_t = osrs_code_to_factor(osrs_t);
    int f_p = osrs_code_to_factor(osrs_p);
    float meas_ms_f = 1.25f + 2.3f * (float)f_t + 2.3f * (float)f_p;
    int meas_ms = (int)(meas_ms_f + 0.5f);

    esp_err_t ret = bmp280_trigger_forced_and_wait(meas_ms);
    if (ret != ESP_OK) return ret;

    uint8_t rx[6] = {0};
    ret = i2c_read_regs(BMP280_REG_PRESS_MSB, rx, sizeof(rx));
    if (ret != ESP_OK) return ret;

    int32_t adc_P = ((int32_t)rx[0] << 12) | ((int32_t)rx[1] << 4) | ((int32_t)rx[2] >> 4);
    int32_t adc_T = ((int32_t)rx[3] << 12) | ((int32_t)rx[4] << 4) | ((int32_t)rx[5] >> 4);

    float temp_c = bmp280_compensate_temperature_c(adc_T);
    float pressure_pa = bmp280_compensate_pressure_pa(adc_P);

    if (isnan(pressure_pa)) {
        return ESP_FAIL;
    }

    if (out_temp_c) *out_temp_c = temp_c;
    if (out_pressure_hpa) *out_pressure_hpa = pressure_pa / 100.0f; /* Pa -> hPa */
    return ESP_OK;
#endif
}

esp_err_t bmp280_driver_reset(void)
{
    if (!s_initialized || s_dev == NULL) {
        /* Allow reset to act as re-init for coordinator auto-recovery */
        return bmp280_driver_init();
    }
    esp_err_t ret = bmp280_soft_reset();
    if (ret != ESP_OK) return ret;
    ret = bmp280_read_calibration();
    if (ret != ESP_OK) return ret;
    ret = bmp280_configure();
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "BMP280 soft reset complete");
    }
    return ret;
}

esp_err_t bmp280_driver_deinit(void)
{
    if (!s_initialized) {
        return ESP_OK;
    }
    if (s_dev) {
        (void)i2c_master_bus_rm_device(s_dev);
        s_dev = NULL;
    }
    memset(&s_calib, 0, sizeof(s_calib));
    s_initialized = false;
    ESP_LOGI(TAG, "BMP280 driver deinitialized");
    return ESP_OK;
}

esp_err_t bmp280_driver_disable(void)
{
    /* Stub implementation - BMP280 has no hardware sleep mode */
    ESP_LOGI(TAG, "BMP280 disabled (no hardware sleep mode)");
    return ESP_OK;
}

esp_err_t bmp280_driver_enable(void)
{
    /* Stub implementation - BMP280 has no hardware sleep mode */
    ESP_LOGI(TAG, "BMP280 enabled (no hardware sleep mode)");
    return ESP_OK;
}
