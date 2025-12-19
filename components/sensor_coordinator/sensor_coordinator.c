/* components/sensor_coordinator/sensor_coordinator.c */
#include <string.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "nvs.h"

#include "sensor_coordinator.h"
#include "iaq_data.h"
#include "iaq_config.h"

/* Driver includes */
#include "i2c_bus.h"
#include "uart_bus.h"
#include "mcu_temp_driver.h"
#include "sht45_driver.h"
#include "bmp280_driver.h"
#include "sgp41_driver.h"
#include "pms5003_driver.h"
#include "s8_driver.h"

/* Fusion and metrics */
#include "sensor_fusion.h"
#include "metrics_calc.h"
#include "esp_task_wdt.h"
#include "iaq_profiler.h"
#include "pm_guard.h"
#include <stdatomic.h>

/* ===== Configuration Constants ===== */

/* Command queue depth */
#define CMD_QUEUE_DEPTH                 8

/* Hardware stabilization delay before transitioning INIT->WARMING (ms) */
#define HARDWARE_STABILIZATION_DELAY_MS 500

/* Auto-recovery exponential backoff parameters */
#define RECOVERY_INITIAL_DELAY_MS       30000   /* 30 seconds */
#define RECOVERY_MAX_DELAY_MS           300000  /* 5 minutes */
#define RECOVERY_BACKOFF_MULTIPLIER     2

/* SGP41 conditioning window during warmup */
#define SGP41_CONDITIONING_WINDOW_US    (10 * 1000000LL)  /* 10 seconds */
#define SGP41_CONDITIONING_INTERVAL_US  1000000           /* 1 Hz */

/* Error threshold before transitioning to ERROR state */
#define SENSOR_ERROR_THRESHOLD          3

/* Timer periods */
#define FUSION_TIMER_PERIOD_US          1000000   /* 1 Hz */
#define METRICS_TIMER_PERIOD_US         5000000   /* 0.2 Hz / 5 seconds */

static const char *TAG = "SENSOR_COORD";

static iaq_system_context_t *s_system_ctx = NULL;
static TaskHandle_t s_sensor_task_handle = NULL;
static _Atomic bool s_initialized = false;
static _Atomic bool s_running = false;

/**
 * Internal per-sensor runtime tracking.
 * This is the internal version; sensor_runtime_info_t is the public-facing copy.
 */
typedef struct {
    sensor_state_t state;
    int64_t warmup_deadline_us;
    int64_t last_read_us;
    uint32_t error_count;
} sensor_runtime_t;

/**
 * Per-sensor auto-recovery tracking.
 * Implements exponential backoff for ERROR state sensors.
 */
typedef struct {
    int64_t last_retry_us;      // Last recovery attempt timestamp
    uint8_t retry_count;         // Number of retries since entering ERROR
    uint32_t next_retry_delay_ms; // Next retry delay (exponential: 30s, 60s, 120s, 300s)
} sensor_recovery_t;

/**
 * Sensor operations table - function pointers for each sensor type.
 * Eliminates switch statements by allowing indexed dispatch.
 */
typedef struct {
    esp_err_t (*enable)(void);
    esp_err_t (*disable)(void);
    esp_err_t (*reset)(void);        /* NULL if not supported (e.g., MCU) */
    esp_err_t (*read)(void);         /* Read wrapper that stores to iaq_data */
    const char *name;                /* "MCU" for logs (uppercase) */
    const char *id_name;             /* "mcu" for API (lowercase) */
    const char *nvs_cadence_key;     /* NVS key for cadence storage */
} sensor_ops_t;

/* Forward declarations for read wrappers */
static esp_err_t read_sensor_mcu(void);
static esp_err_t read_sensor_sht45(void);
static esp_err_t read_sensor_bmp280(void);
static esp_err_t read_sensor_sgp41(void);
static esp_err_t read_sensor_pms5003(void);
static esp_err_t read_sensor_s8(void);

/* Sensor operations table indexed by sensor_id_t */
static const sensor_ops_t s_sensor_ops[SENSOR_ID_MAX] = {
    [SENSOR_ID_MCU] = {
        .enable  = mcu_temp_driver_enable,
        .disable = mcu_temp_driver_disable,
        .reset   = NULL,  /* MCU uses disable/enable cycle */
        .read    = read_sensor_mcu,
        .name    = "MCU",
        .id_name = "mcu",
        .nvs_cadence_key = "cad_mcu",
    },
    [SENSOR_ID_SHT45] = {
        .enable  = sht45_driver_enable,
        .disable = sht45_driver_disable,
        .reset   = sht45_driver_reset,
        .read    = read_sensor_sht45,
        .name    = "SHT45",
        .id_name = "sht45",
        .nvs_cadence_key = "cad_sht45",
    },
    [SENSOR_ID_BMP280] = {
        .enable  = bmp280_driver_enable,
        .disable = bmp280_driver_disable,
        .reset   = bmp280_driver_reset,
        .read    = read_sensor_bmp280,
        .name    = "BMP280",
        .id_name = "bmp280",
        .nvs_cadence_key = "cad_bmp280",
    },
    [SENSOR_ID_SGP41] = {
        .enable  = sgp41_driver_enable,
        .disable = sgp41_driver_disable,
        .reset   = sgp41_driver_reset,
        .read    = read_sensor_sgp41,
        .name    = "SGP41",
        .id_name = "sgp41",
        .nvs_cadence_key = "cad_sgp41",
    },
    [SENSOR_ID_PMS5003] = {
        .enable  = pms5003_driver_enable,
        .disable = pms5003_driver_disable,
        .reset   = pms5003_driver_reset,
        .read    = read_sensor_pms5003,
        .name    = "PMS5003",
        .id_name = "pms5003",
        .nvs_cadence_key = "cad_pms5003",
    },
    [SENSOR_ID_S8] = {
        .enable  = s8_driver_enable,
        .disable = s8_driver_disable,
        .reset   = s8_driver_reset,
        .read    = read_sensor_s8,
        .name    = "S8",
        .id_name = "s8",
        .nvs_cadence_key = "cad_s8",
    },
};

/* Per-sensor runtime state tracking */
static sensor_runtime_t s_runtime[SENSOR_ID_MAX];

/* Per-sensor auto-recovery tracking */
static sensor_recovery_t s_recovery[SENSOR_ID_MAX];

/* Spinlock for protecting runtime info reads (prevents torn 64-bit reads) */
static portMUX_TYPE s_runtime_spinlock = portMUX_INITIALIZER_UNLOCKED;

/* Warm-up durations from Kconfig (milliseconds) */
static const uint32_t s_warmup_ms[SENSOR_ID_MAX] = {
    [SENSOR_ID_MCU]     = CONFIG_IAQ_WARMUP_MCU_MS,
    [SENSOR_ID_SHT45]   = CONFIG_IAQ_WARMUP_SHT45_MS,
    [SENSOR_ID_BMP280]  = CONFIG_IAQ_WARMUP_BMP280_MS,
    [SENSOR_ID_SGP41]   = CONFIG_IAQ_WARMUP_SGP41_MS,
    [SENSOR_ID_PMS5003] = CONFIG_IAQ_WARMUP_PMS5003_MS,
    [SENSOR_ID_S8]      = CONFIG_IAQ_WARMUP_S8_MS,
};

/* Invalidate data for a specific sensor: clear valid flags and set values to sentinels.
 * This ensures downstream publishers/UI do not display stale or default zeros
 * during ERROR/DISABLED/recovery windows. */
static void invalidate_sensor_data(sensor_id_t id)
{
    IAQ_DATA_WITH_LOCK() {
        iaq_data_t *data = iaq_data_get();
        switch (id) {
            case SENSOR_ID_MCU:
                data->valid.mcu_temp_c = false;
                data->raw.mcu_temp_c = NAN;
                break;
            case SENSOR_ID_SHT45:
                data->valid.temp_c = false;
                data->valid.rh_pct = false;
                data->raw.temp_c = NAN;
                data->raw.rh_pct = NAN;
                break;
            case SENSOR_ID_BMP280:
                data->valid.pressure_pa = false;
                data->raw.pressure_pa = NAN;
                break;
            case SENSOR_ID_SGP41:
                data->valid.voc_index = false;
                data->valid.nox_index = false;
                data->raw.voc_index = UINT16_MAX;
                data->raw.nox_index = UINT16_MAX;
                break;
            case SENSOR_ID_PMS5003:
                data->valid.pm1_ugm3 = false;
                data->valid.pm25_ugm3 = false;
                data->valid.pm10_ugm3 = false;
                data->raw.pm1_ugm3 = NAN;
                data->raw.pm25_ugm3 = NAN;
                data->raw.pm10_ugm3 = NAN;
                break;
            case SENSOR_ID_S8:
                data->valid.co2_ppm = false;
                data->raw.co2_ppm = NAN;
                break;
            default:
                break;
        }
    }
}

/* Forward declarations */
static const char* sensor_id_to_string(sensor_id_t id);

typedef enum { CMD_READ = 0, CMD_RESET, CMD_CALIBRATE, CMD_DISABLE, CMD_ENABLE } sensor_cmd_type_t;
typedef struct {
    sensor_cmd_type_t type;
    sensor_id_t id;
    int value;
    QueueHandle_t resp_queue; /* optional: where to send esp_err_t result */
} sensor_cmd_t;

static QueueHandle_t s_cmd_queue = NULL;

typedef struct {
    TickType_t period_ticks;
    TickType_t next_due;
    bool enabled;
} sensor_schedule_t;

static sensor_schedule_t s_schedule[SENSOR_ID_MAX];
static uint32_t s_cadence_ms[SENSOR_ID_MAX] = {0};
static bool s_cadence_from_nvs[SENSOR_ID_MAX] = {0};

#define NVS_NAMESPACE "sensor_cfg"

/* Periodic timers for fusion and metrics processing */
static esp_timer_handle_t s_fusion_timer = NULL;
static esp_timer_handle_t s_metrics_timer = NULL;

static uint32_t load_cadence_ms(const char *key, uint32_t def_ms, bool *from_nvs)
{
    nvs_handle_t h;
    uint32_t val = def_ms;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &h);
    if (err == ESP_OK) {
        uint32_t tmp;
        err = nvs_get_u32(h, key, &tmp);
        if (err == ESP_OK) { val = tmp; if (from_nvs) *from_nvs = true; }
        nvs_close(h);
    }
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        // store default
        if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h) == ESP_OK) {
            nvs_set_u32(h, key, def_ms);
            nvs_commit(h);
            nvs_close(h);
        }
        if (from_nvs) *from_nvs = false;
    }
    return val;
}

static void save_cadence_ms(const char *key, uint32_t ms)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_u32(h, key, ms);
        nvs_commit(h);
        nvs_close(h);
    }
}

/* Default cadences from Kconfig (milliseconds) */
static const uint32_t s_default_cadence_ms[SENSOR_ID_MAX] = {
    [SENSOR_ID_MCU]     = CONFIG_IAQ_CADENCE_MCU_MS,
    [SENSOR_ID_SHT45]   = CONFIG_IAQ_CADENCE_SHT45_MS,
    [SENSOR_ID_BMP280]  = CONFIG_IAQ_CADENCE_BMP280_MS,
    [SENSOR_ID_SGP41]   = CONFIG_IAQ_CADENCE_SGP41_MS,
    [SENSOR_ID_PMS5003] = CONFIG_IAQ_CADENCE_PMS5003_MS,
    [SENSOR_ID_S8]      = CONFIG_IAQ_CADENCE_S8_MS,
};

static void init_schedule_from_config(void)
{
    /* Load cadences from NVS (or defaults) using ops table for keys */
    for (int i = 0; i < SENSOR_ID_MAX; ++i) {
        s_cadence_ms[i] = load_cadence_ms(
            s_sensor_ops[i].nvs_cadence_key,
            s_default_cadence_ms[i],
            &s_cadence_from_nvs[i]
        );
    }

    /* Stagger initial sensor reads to avoid thundering herd on boot.
     * Spread sensors evenly across first period to flatten I2C/UART load. */
    TickType_t now = xTaskGetTickCount();
    for (int i = 0; i < SENSOR_ID_MAX; ++i) {
        s_schedule[i].enabled = (s_cadence_ms[i] > 0);
        s_schedule[i].period_ticks = pdMS_TO_TICKS(s_cadence_ms[i]);
        /* Stagger: sensor i starts at (period * i / MAX) to spread load */
        TickType_t offset = (s_schedule[i].period_ticks * i) / SENSOR_ID_MAX;
        s_schedule[i].next_due = now + offset;
    }
}

/**
 * Transition a sensor to a new state with logging.
 */
static void transition_to_state(sensor_id_t id, sensor_state_t new_state)
{
    if (id < 0 || id >= SENSOR_ID_MAX) return;

    sensor_state_t old_state = s_runtime[id].state;
    if (old_state != new_state) {
        ESP_LOGI(TAG, "Sensor %d: %s -> %s", id,
                 sensor_coordinator_state_to_string(old_state),
                 sensor_coordinator_state_to_string(new_state));
        s_runtime[id].state = new_state;

        /* On transition to WARMING, set warm-up deadline */
        if (new_state == SENSOR_STATE_WARMING) {
            s_runtime[id].warmup_deadline_us = esp_timer_get_time() + (s_warmup_ms[id] * 1000LL);
        }

        /* On transition to READY, reset error count */
        if (new_state == SENSOR_STATE_READY) {
            s_runtime[id].error_count = 0;
        }

        /* Invalidate data in states where values are not trustworthy */
        if (new_state == SENSOR_STATE_ERROR || new_state == SENSOR_STATE_DISABLED) {
            invalidate_sensor_data(id);
        }
    }
}

/**
 * Get sensor name string for logging (uppercase).
 */
static const char* sensor_id_to_string(sensor_id_t id)
{
    if (id >= 0 && id < SENSOR_ID_MAX) {
        return s_sensor_ops[id].name;
    }
    return "UNKNOWN";
}

/**
 * Reset a sensor using the ops table.
 * MCU sensor lacks a reset function; uses disable/enable cycle instead.
 */
static esp_err_t reset_sensor(sensor_id_t id)
{
    if (id < 0 || id >= SENSOR_ID_MAX) {
        return ESP_ERR_INVALID_ARG;
    }

    if (s_sensor_ops[id].reset != NULL) {
        return s_sensor_ops[id].reset();
    }

    /* Fallback for sensors without reset (MCU): disable/enable cycle */
    if (s_sensor_ops[id].disable != NULL && s_sensor_ops[id].enable != NULL) {
        s_sensor_ops[id].disable();
        return s_sensor_ops[id].enable();
    }

    return ESP_ERR_NOT_SUPPORTED;
}


/* ===== Per-Sensor Read Handlers ===== */

static esp_err_t read_sensor_mcu(void)
{
    if (s_runtime[SENSOR_ID_MCU].state != SENSOR_STATE_READY) {
        return ESP_ERR_INVALID_STATE;
    }

    float temp_c = 0.0f;
    uint64_t t0 = iaq_prof_tic();
    esp_err_t ret = mcu_temp_driver_read_celsius(&temp_c);
    iaq_prof_toc(IAQ_METRIC_SENSOR_MCU_READ, t0);

    if (ret == ESP_OK) {
        IAQ_DATA_WITH_LOCK() {
            iaq_data_t *data = iaq_data_get();
            data->raw.mcu_temp_c = temp_c;
            data->updated_at.mcu = esp_timer_get_time();
            data->valid.mcu_temp_c = true;
        }
        s_runtime[SENSOR_ID_MCU].last_read_us = esp_timer_get_time();
        s_runtime[SENSOR_ID_MCU].error_count = 0;
        ESP_LOGD(TAG, "MCU temp: %.1f C", temp_c);
    } else {
        s_runtime[SENSOR_ID_MCU].error_count++;
        if (s_runtime[SENSOR_ID_MCU].error_count >= SENSOR_ERROR_THRESHOLD) {
            ESP_LOGW(TAG, "MCU sensor failed %d times, transitioning to ERROR", SENSOR_ERROR_THRESHOLD);
            transition_to_state(SENSOR_ID_MCU, SENSOR_STATE_ERROR);
        }
    }

    return ret;
}

static esp_err_t read_sensor_sht45(void)
{
    if (s_runtime[SENSOR_ID_SHT45].state != SENSOR_STATE_READY) {
        return ESP_ERR_INVALID_STATE;
    }

    float temp_c = 0.0f, humidity_rh = 0.0f;
    uint64_t t0 = iaq_prof_tic();
    esp_err_t ret = sht45_driver_read(&temp_c, &humidity_rh);
    iaq_prof_toc(IAQ_METRIC_SENSOR_SHT45_READ, t0);

    if (ret == ESP_OK) {
        IAQ_DATA_WITH_LOCK() {
            iaq_data_t *data = iaq_data_get();
            data->raw.temp_c = temp_c;
            data->raw.rh_pct = humidity_rh;
            data->updated_at.sht45 = esp_timer_get_time();
            data->valid.temp_c = true;
            data->valid.rh_pct = true;
        }
        s_runtime[SENSOR_ID_SHT45].last_read_us = esp_timer_get_time();
        s_runtime[SENSOR_ID_SHT45].error_count = 0;
        ESP_LOGD(TAG, "SHT45: %.1f C, %.1f %%RH", temp_c, humidity_rh);
    } else if (ret != ESP_ERR_NOT_SUPPORTED) {
        s_runtime[SENSOR_ID_SHT45].error_count++;
        if (s_runtime[SENSOR_ID_SHT45].error_count >= SENSOR_ERROR_THRESHOLD) {
            ESP_LOGW(TAG, "SHT45 sensor failed %d times, transitioning to ERROR", SENSOR_ERROR_THRESHOLD);
            transition_to_state(SENSOR_ID_SHT45, SENSOR_STATE_ERROR);
        }
    }

    return ret;
}

static esp_err_t read_sensor_bmp280(void)
{
    if (s_runtime[SENSOR_ID_BMP280].state != SENSOR_STATE_READY) {
        return ESP_ERR_INVALID_STATE;
    }

    float pressure_hpa = 0.0f, temp_c = 0.0f;
    uint64_t t0 = iaq_prof_tic();
    esp_err_t ret = bmp280_driver_read(&pressure_hpa, &temp_c);
    iaq_prof_toc(IAQ_METRIC_SENSOR_BMP280_READ, t0);

    if (ret == ESP_OK) {
        IAQ_DATA_WITH_LOCK() {
            iaq_data_t *data = iaq_data_get();
            data->raw.pressure_pa = pressure_hpa * 100.0f;  /* hPa -> Pa */
            data->updated_at.bmp280 = esp_timer_get_time();
            data->valid.pressure_pa = true;
        }
        s_runtime[SENSOR_ID_BMP280].last_read_us = esp_timer_get_time();
        s_runtime[SENSOR_ID_BMP280].error_count = 0;
        ESP_LOGD(TAG, "BMP280: %.1f hPa, %.1f C", pressure_hpa, temp_c);
    } else if (ret != ESP_ERR_NOT_SUPPORTED) {
        s_runtime[SENSOR_ID_BMP280].error_count++;
        if (s_runtime[SENSOR_ID_BMP280].error_count >= SENSOR_ERROR_THRESHOLD) {
            ESP_LOGW(TAG, "BMP280 sensor failed %d times, transitioning to ERROR", SENSOR_ERROR_THRESHOLD);
            transition_to_state(SENSOR_ID_BMP280, SENSOR_STATE_ERROR);
        }
    }

    return ret;
}

static esp_err_t read_sensor_sgp41(void)
{
    if (s_runtime[SENSOR_ID_SGP41].state != SENSOR_STATE_READY) {
        return ESP_ERR_INVALID_STATE;
    }

    /* Get current temperature and humidity for compensation */
    float temp_c = 25.0f, humidity_rh = 50.0f;  /* defaults */
    IAQ_DATA_WITH_LOCK() {
        iaq_data_t *data = iaq_data_get();
        if (data->valid.temp_c) temp_c = data->raw.temp_c;
        if (data->valid.rh_pct) humidity_rh = data->raw.rh_pct;
    }

    uint16_t voc_index = 0, nox_index = 0;
    uint64_t t0 = iaq_prof_tic();
    esp_err_t ret = sgp41_driver_read(&voc_index, &nox_index, temp_c, humidity_rh);
    iaq_prof_toc(IAQ_METRIC_SENSOR_SGP41_READ, t0);

    if (ret == ESP_OK) {
        IAQ_DATA_WITH_LOCK() {
            iaq_data_t *data = iaq_data_get();
            data->raw.voc_index = voc_index;
            data->raw.nox_index = nox_index;
            data->updated_at.sgp41 = esp_timer_get_time();
            data->valid.voc_index = true;
            data->valid.nox_index = true;
        }
        s_runtime[SENSOR_ID_SGP41].last_read_us = esp_timer_get_time();
        s_runtime[SENSOR_ID_SGP41].error_count = 0;
        ESP_LOGD(TAG, "SGP41: VOC=%u, NOx=%u", voc_index, nox_index);
    } else if (ret != ESP_ERR_NOT_SUPPORTED) {
        s_runtime[SENSOR_ID_SGP41].error_count++;
        if (s_runtime[SENSOR_ID_SGP41].error_count >= SENSOR_ERROR_THRESHOLD) {
            ESP_LOGW(TAG, "SGP41 sensor failed %d times, transitioning to ERROR", SENSOR_ERROR_THRESHOLD);
            transition_to_state(SENSOR_ID_SGP41, SENSOR_STATE_ERROR);
        }
    }

    return ret;
}

static esp_err_t read_sensor_pms5003(void)
{
    if (s_runtime[SENSOR_ID_PMS5003].state != SENSOR_STATE_READY) {
        return ESP_ERR_INVALID_STATE;
    }

    float pm1_0 = 0.0f, pm2_5 = 0.0f, pm10 = 0.0f;
    uint64_t t0 = iaq_prof_tic();
    esp_err_t ret = pms5003_driver_read(&pm1_0, &pm2_5, &pm10);
    iaq_prof_toc(IAQ_METRIC_SENSOR_PMS5003_READ, t0);

    if (ret == ESP_OK) {
        IAQ_DATA_WITH_LOCK() {
            iaq_data_t *data = iaq_data_get();
            data->raw.pm1_ugm3 = pm1_0;
            data->raw.pm25_ugm3 = pm2_5;
            data->raw.pm10_ugm3 = pm10;
            data->updated_at.pms5003 = esp_timer_get_time();
            data->valid.pm1_ugm3 = true;
            data->valid.pm25_ugm3 = true;
            data->valid.pm10_ugm3 = true;
        }
        s_runtime[SENSOR_ID_PMS5003].last_read_us = esp_timer_get_time();
        s_runtime[SENSOR_ID_PMS5003].error_count = 0;
        ESP_LOGD(TAG, "PMS5003: PM1.0=%.0f, PM2.5=%.0f, PM10=%.0f ug/m3", pm1_0, pm2_5, pm10);
    } else if (ret != ESP_ERR_NOT_SUPPORTED) {
        s_runtime[SENSOR_ID_PMS5003].error_count++;
        if (s_runtime[SENSOR_ID_PMS5003].error_count >= SENSOR_ERROR_THRESHOLD) {
            ESP_LOGW(TAG, "PMS5003 sensor failed %d times, transitioning to ERROR", SENSOR_ERROR_THRESHOLD);
            transition_to_state(SENSOR_ID_PMS5003, SENSOR_STATE_ERROR);
        }
    }

    return ret;
}

static esp_err_t read_sensor_s8(void)
{
    if (s_runtime[SENSOR_ID_S8].state != SENSOR_STATE_READY) {
        return ESP_ERR_INVALID_STATE;
    }

    float co2_ppm = 0.0f;
    uint64_t t0 = iaq_prof_tic();
    esp_err_t ret = s8_driver_read_co2(&co2_ppm);
    iaq_prof_toc(IAQ_METRIC_SENSOR_S8_READ, t0);

    if (ret == ESP_OK) {
        IAQ_DATA_WITH_LOCK() {
            iaq_data_t *data = iaq_data_get();
            data->raw.co2_ppm = co2_ppm;
            data->updated_at.s8 = esp_timer_get_time();
            data->valid.co2_ppm = true;
        }
        s_runtime[SENSOR_ID_S8].last_read_us = esp_timer_get_time();
        s_runtime[SENSOR_ID_S8].error_count = 0;
        ESP_LOGD(TAG, "S8 CO2: %.0f ppm", co2_ppm);
    } else if (ret != ESP_ERR_NOT_SUPPORTED) {
        s_runtime[SENSOR_ID_S8].error_count++;
        if (s_runtime[SENSOR_ID_S8].error_count >= SENSOR_ERROR_THRESHOLD) {
            ESP_LOGW(TAG, "S8 sensor failed %d times, transitioning to ERROR", SENSOR_ERROR_THRESHOLD);
            transition_to_state(SENSOR_ID_S8, SENSOR_STATE_ERROR);
        }
    }

    return ret;
}

/**
 * Fusion timer callback (runs at 1 Hz).
 * Applies cross-sensor compensations to raw sensor data.
 */
static void fusion_timer_callback(void *arg)
{
    iaq_prof_ctx_t p = iaq_prof_start(IAQ_METRIC_FUSION_TICK);
    if (iaq_data_lock(0)) {
        iaq_data_t *data = iaq_data_get();
        fusion_apply(data);
        iaq_data_unlock();
    }
    iaq_prof_end(p);
}

/**
 * Metrics timer callback (runs at 0.2 Hz / every 5 seconds).
 * Calculates all derived metrics (AQI, comfort, trends).
 * Metrics are calculated and stored in iaq_data; MQTT publishing uses timer-based intervals.
 */
static void metrics_timer_callback(void *arg)
{
    iaq_prof_ctx_t p = iaq_prof_start(IAQ_METRIC_METRICS_TICK);
    if (iaq_data_lock(0)) {
        iaq_data_t *data = iaq_data_get();
        metrics_calculate_all(data);
        iaq_data_unlock();
    }
    iaq_prof_end(p);
}

/**
 * Sensor coordinator task.
 * Manages sensor state machine, warm-up periods, and periodic reads.
 */
static void sensor_coordinator_task(void *arg)
{
    ESP_LOGI(TAG, "Sensor coordinator task started");

    /* Subscribe this task to the Task Watchdog Timer for deadlock detection */
    esp_err_t wdt_ret = esp_task_wdt_add(NULL);
    if (wdt_ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to add sensor coordinator to TWDT: %s", esp_err_to_name(wdt_ret));
    }

    /* Brief delay for hardware stabilization */
    vTaskDelay(pdMS_TO_TICKS(HARDWARE_STABILIZATION_DELAY_MS));

    /* Transition sensors from INIT -> WARMING */
    for (int i = 0; i < SENSOR_ID_MAX; ++i) {
        if (s_runtime[i].state == SENSOR_STATE_INIT) {
            if (s_warmup_ms[i] > 0) {
                transition_to_state(i, SENSOR_STATE_WARMING);
            } else {
                /* No warm-up needed, go straight to READY */
                transition_to_state(i, SENSOR_STATE_READY);
            }
        }
    }

    /* Signal that coordinator task has started (individual sensors may still be warming up) */
    xEventGroupSetBits(s_system_ctx->event_group, SENSOR_COORDINATOR_STARTED_BIT);

    while (s_running) {
        /* Reset watchdog - confirms task is still running and not deadlocked */
        if (wdt_ret == ESP_OK) {
            esp_task_wdt_reset();
        }

        int64_t now_us = esp_timer_get_time();

        /* For SGP41, run 1 Hz conditioning ticks during WARMING (first 10s only) */
        if (s_runtime[SENSOR_ID_SGP41].state == SENSOR_STATE_WARMING) {
            int64_t warmup_start_us = s_runtime[SENSOR_ID_SGP41].warmup_deadline_us -
                                      ((int64_t)s_warmup_ms[SENSOR_ID_SGP41] * 1000LL);
            /* Avoid scheduling conditioning past the hardware window */
            if ((now_us - warmup_start_us) <= SGP41_CONDITIONING_WINDOW_US &&
                (now_us - s_runtime[SENSOR_ID_SGP41].last_read_us) >= SGP41_CONDITIONING_INTERVAL_US) {
                /* Use last known temp/RH for compensation; fallback defaults */
                float temp_c = 25.0f, humidity_rh = 50.0f;
                IAQ_DATA_WITH_LOCK() {
                    iaq_data_t *data = iaq_data_get();
                    if (data->valid.temp_c) temp_c = data->raw.temp_c;
                    if (data->valid.rh_pct) humidity_rh = data->raw.rh_pct;
                }
                (void)sgp41_driver_conditioning_tick(temp_c, humidity_rh);
                s_runtime[SENSOR_ID_SGP41].last_read_us = now_us;
            }
        }

        /* Check warm-up deadlines, promote WARMING -> READY */
        for (int i = 0; i < SENSOR_ID_MAX; ++i) {
            if (s_runtime[i].state == SENSOR_STATE_WARMING) {
                if (now_us >= s_runtime[i].warmup_deadline_us) {
                    bool ready = true;
                    if (i == SENSOR_ID_SGP41) {
                        ready = sgp41_driver_is_reporting_ready();
                    }
                    if (ready) {
                        ESP_LOGI(TAG, "%s warm-up complete", sensor_id_to_string(i));
                        transition_to_state(i, SENSOR_STATE_READY);
                    }
                }
            }
        }

        /* Auto-recovery: periodically retry ERROR state sensors with exponential backoff */
        for (int i = 0; i < SENSOR_ID_MAX; ++i) {
            /* Skip DISABLED sensors - they should not auto-recover */
            if (s_runtime[i].state == SENSOR_STATE_DISABLED) {
                continue;
            }
            if (s_runtime[i].state == SENSOR_STATE_ERROR) {
                int64_t time_since_retry_us = now_us - s_recovery[i].last_retry_us;
                uint64_t retry_delay_us = (uint64_t)s_recovery[i].next_retry_delay_ms * 1000ULL;

                if (time_since_retry_us >= retry_delay_us || s_recovery[i].last_retry_us == 0) {
                    ESP_LOGI(TAG, "%s auto-recovery attempt %d (backoff: %d ms)",
                             sensor_id_to_string(i), s_recovery[i].retry_count + 1,
                             s_recovery[i].next_retry_delay_ms);

                    /* Attempt driver reset via ops table */
                    esp_err_t reset_result = reset_sensor((sensor_id_t)i);

                    s_recovery[i].last_retry_us = now_us;
                    s_recovery[i].retry_count++;

                    if (reset_result == ESP_OK) {
                        ESP_LOGI(TAG, "%s auto-recovery succeeded", sensor_id_to_string(i));
                        /* Reset successful - transition to INIT/WARMING */
                        if (s_warmup_ms[i] > 0) {
                            transition_to_state(i, SENSOR_STATE_WARMING);
                        } else {
                            transition_to_state(i, SENSOR_STATE_READY);
                        }
                        /* After recovery, mark values invalid until next good read */
                        invalidate_sensor_data((sensor_id_t)i);
                        /* Reset recovery tracking */
                        s_recovery[i].retry_count = 0;
                        s_recovery[i].next_retry_delay_ms = RECOVERY_INITIAL_DELAY_MS;
                    } else {
                        /* Recovery failed - exponential backoff */
                        if (s_recovery[i].next_retry_delay_ms < RECOVERY_MAX_DELAY_MS) {
                            s_recovery[i].next_retry_delay_ms *= RECOVERY_BACKOFF_MULTIPLIER;
                            if (s_recovery[i].next_retry_delay_ms > RECOVERY_MAX_DELAY_MS) {
                                s_recovery[i].next_retry_delay_ms = RECOVERY_MAX_DELAY_MS;
                            }
                        }
                        ESP_LOGW(TAG, "%s auto-recovery failed, next retry in %d ms",
                                 sensor_id_to_string(i), s_recovery[i].next_retry_delay_ms);
                    }
                }
            }
        }

        /* Calculate time until next sensor is due */
        TickType_t now = xTaskGetTickCount();
        TickType_t next_wake = portMAX_DELAY;

        for (int i = 0; i < SENSOR_ID_MAX; ++i) {
            if (!s_schedule[i].enabled) {
                continue;
            }
            if (s_runtime[i].state == SENSOR_STATE_DISABLED) {
                continue;
            }
            TickType_t time_until_due = s_schedule[i].next_due - now;
            /* Handle tick overflow and past-due sensors */
            if ((int32_t)time_until_due <= 0) {
                next_wake = 0;  /* Sensor is due now */
                break;
            } else if (time_until_due < next_wake) {
                next_wake = time_until_due;
            }
        }

        /* Handle pending coordinator commands with calculated timeout */
        sensor_cmd_t cmd;
        /* Ensure periodic wakeups even when no sensors are scheduled to allow clean shutdowns. */
        TickType_t queue_timeout = (next_wake == 0) ? 1 : next_wake;
        if (next_wake == portMAX_DELAY) {
            queue_timeout = pdMS_TO_TICKS(1000); /* wake at least once per second */
        }
        if (s_cmd_queue && xQueueReceive(s_cmd_queue, &cmd, queue_timeout) == pdTRUE) {
            if (!s_running) {
                /* Stop requested while blocked on queue; skip processing. */
                continue;
            }
            esp_err_t op_res = ESP_ERR_NOT_SUPPORTED;
            switch (cmd.type) {
                case CMD_READ:
                    /* Dispatch to per-sensor read handler via ops table */
                    if (cmd.id >= 0 && cmd.id < SENSOR_ID_MAX && s_sensor_ops[cmd.id].read) {
                        op_res = s_sensor_ops[cmd.id].read();
                    } else {
                        op_res = ESP_ERR_INVALID_ARG;
                    }
                    break;
                case CMD_RESET:
                    /* Attempt to recover ERROR state sensors via ops table */
                    op_res = reset_sensor(cmd.id);
                    if (op_res == ESP_OK) {
                        transition_to_state(cmd.id, SENSOR_STATE_READY);
                        invalidate_sensor_data(cmd.id);
                    }
                    break;
                case CMD_CALIBRATE:
                    /* Calibration is sensor-specific (only S8 supports it currently) */
                    if (cmd.id == SENSOR_ID_S8) {
                        op_res = s8_driver_calibrate_co2(cmd.value);
                    }
                    break;
                case CMD_DISABLE:
                    /* Call driver disable function via ops table */
                    if (cmd.id >= 0 && cmd.id < SENSOR_ID_MAX && s_sensor_ops[cmd.id].disable) {
                        op_res = s_sensor_ops[cmd.id].disable();
                    } else {
                        op_res = ESP_ERR_INVALID_ARG;
                    }
                    if (op_res == ESP_OK) {
                        transition_to_state(cmd.id, SENSOR_STATE_DISABLED);
                        invalidate_sensor_data(cmd.id);
                    }
                    break;
                case CMD_ENABLE:
                    /* Call driver enable function via ops table */
                    if (cmd.id >= 0 && cmd.id < SENSOR_ID_MAX && s_sensor_ops[cmd.id].enable) {
                        op_res = s_sensor_ops[cmd.id].enable();
                    } else {
                        op_res = ESP_ERR_INVALID_ARG;
                    }
                    if (op_res == ESP_OK) {
                        /* Transition to WARMING if warm-up needed, otherwise READY */
                        if (s_warmup_ms[cmd.id] > 0) {
                            transition_to_state(cmd.id, SENSOR_STATE_WARMING);
                        } else {
                            transition_to_state(cmd.id, SENSOR_STATE_READY);
                        }
                        invalidate_sensor_data(cmd.id);
                    }
                    break;
                default:
                    break;
            }
            if (cmd.resp_queue) {
                (void)xQueueSend(cmd.resp_queue, &op_res, 0);
            }
            /* After processing command, continue to check for more or handle scheduled reads */
            continue;
        }

        /* Periodic scheduler: read sensors that are READY and due */
        now = xTaskGetTickCount();
        for (int i = 0; i < SENSOR_ID_MAX; ++i) {
            /* Only read if sensor is READY, enabled, and due (DISABLED sensors are excluded by state check) */
            if (s_runtime[i].state == SENSOR_STATE_READY &&
                s_schedule[i].enabled &&
                (int32_t)(now - s_schedule[i].next_due) >= 0) {

                /* Dispatch to read handler via ops table */
                if (s_sensor_ops[i].read) {
                    (void)s_sensor_ops[i].read();
                }

                /* Increment from previous due time to maintain cadence without drift */
                s_schedule[i].next_due += s_schedule[i].period_ticks;
            }
        }
    }

    ESP_LOGI(TAG, "Sensor coordinator task stopped");
    s_sensor_task_handle = NULL;
    vTaskDelete(NULL);
}

esp_err_t sensor_coordinator_init(iaq_system_context_t *ctx)
{
    if (!ctx) {
        ESP_LOGE(TAG, "Invalid system context");
        return ESP_ERR_INVALID_ARG;
    }

    if (s_initialized) {
        ESP_LOGW(TAG, "Sensor coordinator already initialized");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Initializing sensor coordinator");

    /* Store system context */
    s_system_ctx = ctx;

    /* Initialize all sensor runtime states to UNINIT */
    for (int i = 0; i < SENSOR_ID_MAX; ++i) {
        memset(&s_runtime[i], 0, sizeof(sensor_runtime_t));
        s_runtime[i].state = SENSOR_STATE_UNINIT;

        /* Initialize auto-recovery tracking */
        s_recovery[i].last_retry_us = 0;
        s_recovery[i].retry_count = 0;
        s_recovery[i].next_retry_delay_ms = RECOVERY_INITIAL_DELAY_MS;
    }

    /* Create command queue */
    s_cmd_queue = xQueueCreate(CMD_QUEUE_DEPTH, sizeof(sensor_cmd_t));
    if (s_cmd_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create sensor command queue");
        return ESP_FAIL;
    }

#ifdef CONFIG_IAQ_SIMULATION
    ESP_LOGW(TAG, "*** SIMULATION MODE ENABLED - Using fake sensor data ***");
#endif

    /* Initialize I2C bus for SHT4x (SHT45), BMP280, SGP41 */
    esp_err_t ret = i2c_bus_init();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "I2C bus init failed: %s (I2C sensors disabled)", esp_err_to_name(ret));
        transition_to_state(SENSOR_ID_SHT45, SENSOR_STATE_ERROR);
        transition_to_state(SENSOR_ID_BMP280, SENSOR_STATE_ERROR);
        transition_to_state(SENSOR_ID_SGP41, SENSOR_STATE_ERROR);
    } else {
        ESP_LOGI(TAG, "I2C bus initialized successfully");
        i2c_bus_probe();  /* Log detected devices */

        /* Initialize SHT45 driver */
        ret = sht45_driver_init();
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "SHT45 driver init failed: %s", esp_err_to_name(ret));
            transition_to_state(SENSOR_ID_SHT45, SENSOR_STATE_ERROR);
        } else {
            ESP_LOGI(TAG, "SHT45 driver initialized");
            transition_to_state(SENSOR_ID_SHT45, SENSOR_STATE_INIT);
        }

        /* Initialize BMP280 driver */
        ret = bmp280_driver_init();
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "BMP280 driver init failed: %s", esp_err_to_name(ret));
            transition_to_state(SENSOR_ID_BMP280, SENSOR_STATE_ERROR);
        } else {
            ESP_LOGI(TAG, "BMP280 driver initialized");
            transition_to_state(SENSOR_ID_BMP280, SENSOR_STATE_INIT);
        }

        /* Initialize SGP41 driver */
        ret = sgp41_driver_init();
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "SGP41 driver init failed: %s", esp_err_to_name(ret));
            transition_to_state(SENSOR_ID_SGP41, SENSOR_STATE_ERROR);
        } else {
            ESP_LOGI(TAG, "SGP41 driver initialized");
            transition_to_state(SENSOR_ID_SGP41, SENSOR_STATE_INIT);
        }
    }

    /* Initialize PMS5003 driver (includes UART init) */
    ret = pms5003_driver_init();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "PMS5003 driver init failed: %s", esp_err_to_name(ret));
        transition_to_state(SENSOR_ID_PMS5003, SENSOR_STATE_ERROR);
    } else {
        ESP_LOGI(TAG, "PMS5003 driver initialized");
        transition_to_state(SENSOR_ID_PMS5003, SENSOR_STATE_INIT);
    }

    /* Initialize S8 driver (includes UART init) */
    ret = s8_driver_init();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "S8 driver init failed: %s", esp_err_to_name(ret));
        transition_to_state(SENSOR_ID_S8, SENSOR_STATE_ERROR);
    } else {
        ESP_LOGI(TAG, "S8 driver initialized");
        transition_to_state(SENSOR_ID_S8, SENSOR_STATE_INIT);
        /* Query diagnostics once at init time for logs */
        s8_diag_t diag;
        if (s8_driver_get_diag(&diag) == ESP_OK) {
            ESP_LOGI(TAG, "S8: addr=%u, serial=%u, ABC %s (period=%u h)",
                     diag.modbus_addr, (unsigned)diag.serial_number,
                     diag.abc_enabled ? "enabled" : "disabled",
                     (unsigned)diag.abc_period_hours);
            IAQ_DATA_WITH_LOCK() {
                iaq_data_t *data = iaq_data_get();
                data->hw_diag.s8_diag_valid = true;
                data->hw_diag.s8_addr = diag.modbus_addr;
                data->hw_diag.s8_meter_status = diag.meter_status;
                data->hw_diag.s8_serial = diag.serial_number;
                data->hw_diag.s8_abc_period_hours = diag.abc_period_hours;
                data->hw_diag.s8_abc_enabled = diag.abc_enabled;
            }
        }
    }

    /* Initialize MCU temperature sensor */
    ret = mcu_temp_driver_init(-10, 80);
    if (ret == ESP_OK) {
        ret = mcu_temp_driver_enable();
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "MCU temperature sensor enabled");
            transition_to_state(SENSOR_ID_MCU, SENSOR_STATE_INIT);
        } else {
            ESP_LOGW(TAG, "Failed to enable MCU temperature sensor");
            transition_to_state(SENSOR_ID_MCU, SENSOR_STATE_ERROR);
        }
    } else {
        ESP_LOGW(TAG, "Failed to initialize MCU temperature sensor");
        transition_to_state(SENSOR_ID_MCU, SENSOR_STATE_ERROR);
    }

    /* Initialize schedules and defaults from Kconfig + NVS */
    init_schedule_from_config();

    /* Initialize sensor fusion subsystem */
    ret = fusion_init();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Sensor fusion initialization failed: %s", esp_err_to_name(ret));
        /* Non-fatal - continue without fusion */
    }

    /* Initialize metrics calculation subsystem */
    ret = metrics_init();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Metrics initialization failed: %s", esp_err_to_name(ret));
        /* Non-fatal - continue without metrics */
    }

    /* Create periodic timer for fusion (1 Hz) */
    const esp_timer_create_args_t fusion_timer_args = {
        .callback = fusion_timer_callback,
        .name = "fusion"
    };
    ret = esp_timer_create(&fusion_timer_args, &s_fusion_timer);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create fusion timer: %s", esp_err_to_name(ret));
        return ret;
    }

    /* Create periodic timer for metrics (0.2 Hz / 5 seconds) */
    const esp_timer_create_args_t metrics_timer_args = {
        .callback = metrics_timer_callback,
        .name = "metrics"
    };
    ret = esp_timer_create(&metrics_timer_args, &s_metrics_timer);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create metrics timer: %s", esp_err_to_name(ret));
        esp_timer_delete(s_fusion_timer);
        s_fusion_timer = NULL;
        return ret;
    }

    /* Mark as initialized */
    s_initialized = true;

    ESP_LOGI(TAG, "Sensor coordinator initialized");
    return ESP_OK;
}

esp_err_t sensor_coordinator_start(void)
{
    if (!s_initialized) {
        ESP_LOGE(TAG, "Sensor coordinator not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (s_sensor_task_handle != NULL) {
        ESP_LOGW(TAG, "Sensor coordinator already running");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Starting sensor coordinator task");

    s_running = true;

    BaseType_t ret = xTaskCreatePinnedToCore(
        sensor_coordinator_task,
        "sensor_coord",
        TASK_STACK_SENSOR_COORDINATOR,
        NULL,
        TASK_PRIORITY_SENSOR_COORDINATOR,
        &s_sensor_task_handle,
        TASK_CORE_SENSOR_COORDINATOR
    );

    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create sensor coordinator task");
        s_running = false;
        return ESP_FAIL;
    }

    /* Register task for stack HWM reporting */
    iaq_profiler_register_task("sensor_coord", s_sensor_task_handle, TASK_STACK_SENSOR_COORDINATOR);

    /* Start fusion timer */
    if (s_fusion_timer) {
        esp_err_t timer_ret = esp_timer_start_periodic(s_fusion_timer, FUSION_TIMER_PERIOD_US);
        if (timer_ret != ESP_OK) {
            ESP_LOGW(TAG, "Failed to start fusion timer: %s", esp_err_to_name(timer_ret));
        } else {
            ESP_LOGI(TAG, "Fusion timer started (1 Hz)");
        }
    }

    /* Start metrics timer */
    if (s_metrics_timer) {
        esp_err_t timer_ret = esp_timer_start_periodic(s_metrics_timer, METRICS_TIMER_PERIOD_US);
        if (timer_ret != ESP_OK) {
            ESP_LOGW(TAG, "Failed to start metrics timer: %s", esp_err_to_name(timer_ret));
        } else {
            ESP_LOGI(TAG, "Metrics timer started (0.2 Hz / 5s)");
        }
    }

    ESP_LOGD(TAG, "Sensor coordinator task created");
    return ESP_OK;
}

esp_err_t sensor_coordinator_stop(void)
{
    if (s_sensor_task_handle == NULL) {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Stopping sensor coordinator");
    s_running = false;

    /* Wake the coordinator task if it is blocked on the queue. */
    if (s_cmd_queue) {
        sensor_cmd_t wake_cmd = {0};
        (void)xQueueSend(s_cmd_queue, &wake_cmd, 0);
    }

    /* Stop timers */
    if (s_fusion_timer) {
        esp_timer_stop(s_fusion_timer);
    }
    if (s_metrics_timer) {
        esp_timer_stop(s_metrics_timer);
    }

    /* Wait for task to finish */
    vTaskDelay(pdMS_TO_TICKS(150));

    /* Deinitialize sensors */
    if (s_runtime[SENSOR_ID_MCU].state != SENSOR_STATE_UNINIT) {
        mcu_temp_driver_disable();
        mcu_temp_driver_deinit();
        transition_to_state(SENSOR_ID_MCU, SENSOR_STATE_UNINIT);
    }

    if (s_runtime[SENSOR_ID_SHT45].state != SENSOR_STATE_UNINIT) {
        sht45_driver_deinit();
        transition_to_state(SENSOR_ID_SHT45, SENSOR_STATE_UNINIT);
    }

    if (s_runtime[SENSOR_ID_BMP280].state != SENSOR_STATE_UNINIT) {
        bmp280_driver_deinit();
        transition_to_state(SENSOR_ID_BMP280, SENSOR_STATE_UNINIT);
    }

    if (s_runtime[SENSOR_ID_SGP41].state != SENSOR_STATE_UNINIT) {
        sgp41_driver_deinit();
        transition_to_state(SENSOR_ID_SGP41, SENSOR_STATE_UNINIT);
    }

    if (s_runtime[SENSOR_ID_PMS5003].state != SENSOR_STATE_UNINIT) {
        pms5003_driver_deinit();
        transition_to_state(SENSOR_ID_PMS5003, SENSOR_STATE_UNINIT);
    }

    if (s_runtime[SENSOR_ID_S8].state != SENSOR_STATE_UNINIT) {
        s8_driver_deinit();
        transition_to_state(SENSOR_ID_S8, SENSOR_STATE_UNINIT);
    }

    /* De-initialize buses */
    i2c_bus_deinit();
    /* PMS5003 UART deinitialized by pms5003_driver_deinit() */
    /* S8 UART deinitialized by s8_driver_deinit() */

    if (s_cmd_queue) {
        vQueueDelete(s_cmd_queue);
        s_cmd_queue = NULL;
    }
    return ESP_OK;
}


/* ===== Coordinator Control API ===== */
static esp_err_t enqueue_cmd(sensor_cmd_type_t type, sensor_id_t id, int value, QueueHandle_t resp_queue)
{
    if (!s_initialized || !s_cmd_queue) return ESP_ERR_INVALID_STATE;
    if (id < 0 || id >= SENSOR_ID_MAX) return ESP_ERR_INVALID_ARG;
    sensor_cmd_t cmd = { .type = type, .id = id, .value = value, .resp_queue = resp_queue };
    return xQueueSend(s_cmd_queue, &cmd, pdMS_TO_TICKS(100)) == pdTRUE ? ESP_OK : ESP_ERR_TIMEOUT;
}

esp_err_t sensor_coordinator_force_read(sensor_id_t id)
{
    return enqueue_cmd(CMD_READ, id, 0, NULL);
}

esp_err_t sensor_coordinator_reset(sensor_id_t id)
{
    return enqueue_cmd(CMD_RESET, id, 0, NULL);
}

esp_err_t sensor_coordinator_calibrate(sensor_id_t id, int value)
{
    /* Gate calibration: only accept if sensor is in READY state */
    if (id < 0 || id >= SENSOR_ID_MAX) {
        return ESP_ERR_INVALID_ARG;
    }

    if (s_runtime[id].state != SENSOR_STATE_READY) {
        ESP_LOGW(TAG, "Cannot calibrate %s: sensor not ready (state=%s)",
                 sensor_id_to_string(id),
                 sensor_coordinator_state_to_string(s_runtime[id].state));
        return ESP_ERR_INVALID_STATE;
    }

    return enqueue_cmd(CMD_CALIBRATE, id, value, NULL);
}

esp_err_t sensor_coordinator_disable(sensor_id_t id)
{
    if (id < 0 || id >= SENSOR_ID_MAX) {
        return ESP_ERR_INVALID_ARG;
    }
    return enqueue_cmd(CMD_DISABLE, id, 0, NULL);
}

esp_err_t sensor_coordinator_enable(sensor_id_t id)
{
    if (id < 0 || id >= SENSOR_ID_MAX) {
        return ESP_ERR_INVALID_ARG;
    }
    return enqueue_cmd(CMD_ENABLE, id, 0, NULL);
}

esp_err_t sensor_coordinator_set_cadence(sensor_id_t id, uint32_t interval_ms)
{
    if (id < 0 || id >= SENSOR_ID_MAX) return ESP_ERR_INVALID_ARG;
    if (!s_initialized) return ESP_ERR_INVALID_STATE;
    uint32_t prev_ms = s_cadence_ms[id];
    s_schedule[id].enabled = (interval_ms > 0);
    s_schedule[id].period_ticks = pdMS_TO_TICKS(interval_ms);
    s_schedule[id].next_due = xTaskGetTickCount() + s_schedule[id].period_ticks;
    s_cadence_ms[id] = interval_ms;
    s_cadence_from_nvs[id] = true; /* persisted */
    save_cadence_ms(s_sensor_ops[id].nvs_cadence_key, interval_ms);
    /* If SGP41 cadence changed, reinitialize algorithm sampling interval */
    if (id == SENSOR_ID_SGP41 && prev_ms != interval_ms) {
        (void)sgp41_driver_reset();
    }
    return ESP_OK;
}

esp_err_t sensor_coordinator_get_cadences(uint32_t out_ms[SENSOR_ID_MAX], bool out_from_nvs[SENSOR_ID_MAX])
{
    if (!s_initialized) return ESP_ERR_INVALID_STATE;
    if (out_ms) {
        for (int i = 0; i < SENSOR_ID_MAX; ++i) out_ms[i] = s_cadence_ms[i];
    }
    if (out_from_nvs) {
        for (int i = 0; i < SENSOR_ID_MAX; ++i) out_from_nvs[i] = s_cadence_from_nvs[i];
    }
    return ESP_OK;
}

esp_err_t sensor_coordinator_force_read_sync(sensor_id_t id, uint32_t timeout_ms)
{
    if (!s_initialized || !s_cmd_queue) return ESP_ERR_INVALID_STATE;
    QueueHandle_t q = xQueueCreate(1, sizeof(esp_err_t));
    if (!q) return ESP_ERR_NO_MEM;
    esp_err_t send_res = enqueue_cmd(CMD_READ, id, 0, q);
    if (send_res != ESP_OK) { vQueueDelete(q); return send_res; }
    esp_err_t op_res = ESP_ERR_TIMEOUT;
    TickType_t to = (timeout_ms == portMAX_DELAY) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    if (xQueueReceive(q, &op_res, to) != pdTRUE) {
        op_res = ESP_ERR_TIMEOUT;
    }
    vQueueDelete(q);
    return op_res;
}

esp_err_t sensor_coordinator_get_runtime_info(sensor_id_t id, sensor_runtime_info_t *out_info)
{
    if (id < 0 || id >= SENSOR_ID_MAX || !out_info) {
        return ESP_ERR_INVALID_ARG;
    }

    /* Protect 64-bit timestamp reads with critical section to prevent torn reads.
     * ESP32 does not guarantee atomicity for 64-bit accesses. */
    portENTER_CRITICAL(&s_runtime_spinlock);
    out_info->state = s_runtime[id].state;
    out_info->warmup_deadline_us = s_runtime[id].warmup_deadline_us;
    out_info->last_read_us = s_runtime[id].last_read_us;
    out_info->error_count = s_runtime[id].error_count;
    portEXIT_CRITICAL(&s_runtime_spinlock);

    return ESP_OK;
}

bool sensor_coordinator_any_ready(void)
{
    if (!s_initialized) return false;
    bool ready = false;
    portENTER_CRITICAL(&s_runtime_spinlock);
    for (int i = 0; i < SENSOR_ID_MAX; ++i) {
        if (s_runtime[i].state == SENSOR_STATE_READY) {
            ready = true;
            break;
        }
    }
    portEXIT_CRITICAL(&s_runtime_spinlock);
    return ready;
}

const char* sensor_coordinator_state_to_string(sensor_state_t state)
{
    switch (state) {
        case SENSOR_STATE_UNINIT:  return "UNINIT";
        case SENSOR_STATE_INIT:    return "INIT";
        case SENSOR_STATE_WARMING: return "WARMING";
        case SENSOR_STATE_READY:   return "READY";
        case SENSOR_STATE_ERROR:   return "ERROR";
        case SENSOR_STATE_DISABLED: return "DISABLED";
        default:                   return "UNKNOWN";
    }
}

const char* sensor_coordinator_id_to_name(sensor_id_t id)
{
    if (id >= 0 && id < SENSOR_ID_MAX) {
        return s_sensor_ops[id].id_name;
    }
    return "unknown";
}

uint32_t sensor_coordinator_get_warmup_ms(sensor_id_t id)
{
    if (id >= 0 && id < SENSOR_ID_MAX) {
        return s_warmup_ms[id];
    }
    return 0;
}
