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
#include "s8_driver.h"

#ifdef CONFIG_IAQ_SIMULATION
#include "sensor_sim.h"
#endif

static const char *TAG = "SENSOR_COORD";

static iaq_system_context_t *s_system_ctx = NULL;
static TaskHandle_t s_sensor_task_handle = NULL;
static volatile bool s_initialized = false;
static volatile bool s_running = false;

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

/* Per-sensor runtime state tracking */
static sensor_runtime_t s_runtime[SENSOR_ID_MAX];

/* Warm-up durations from Kconfig (milliseconds) */
static const uint32_t s_warmup_ms[SENSOR_ID_MAX] = {
    [SENSOR_ID_MCU]     = CONFIG_IAQ_WARMUP_MCU_MS,
    [SENSOR_ID_SHT41]   = CONFIG_IAQ_WARMUP_SHT41_MS,
    [SENSOR_ID_BMP280]  = CONFIG_IAQ_WARMUP_BMP280_MS,
    [SENSOR_ID_SGP41]   = CONFIG_IAQ_WARMUP_SGP41_MS,
    [SENSOR_ID_PMS5003] = CONFIG_IAQ_WARMUP_PMS5003_MS,
    [SENSOR_ID_S8]      = CONFIG_IAQ_WARMUP_S8_MS,
};

/* Error threshold before transitioning to ERROR state */
#define ERROR_THRESHOLD 3

/* Forward declarations */
static const char* state_to_string(sensor_state_t state);
static const char* sensor_id_to_string(sensor_id_t id);

typedef enum { CMD_READ = 0, CMD_RESET, CMD_CALIBRATE } sensor_cmd_type_t;
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

static void init_schedule_from_config(void)
{
    s_cadence_ms[SENSOR_ID_MCU]     = load_cadence_ms("cad_mcu",     CONFIG_IAQ_CADENCE_MCU_MS,     &s_cadence_from_nvs[SENSOR_ID_MCU]);
    s_cadence_ms[SENSOR_ID_SHT41]   = load_cadence_ms("cad_sht41",   CONFIG_IAQ_CADENCE_SHT41_MS,   &s_cadence_from_nvs[SENSOR_ID_SHT41]);
    s_cadence_ms[SENSOR_ID_BMP280]  = load_cadence_ms("cad_bmp280",  CONFIG_IAQ_CADENCE_BMP280_MS,  &s_cadence_from_nvs[SENSOR_ID_BMP280]);
    s_cadence_ms[SENSOR_ID_SGP41]   = load_cadence_ms("cad_sgp41",   CONFIG_IAQ_CADENCE_SGP41_MS,   &s_cadence_from_nvs[SENSOR_ID_SGP41]);
    s_cadence_ms[SENSOR_ID_PMS5003] = load_cadence_ms("cad_pms5003", CONFIG_IAQ_CADENCE_PMS5003_MS, &s_cadence_from_nvs[SENSOR_ID_PMS5003]);
    s_cadence_ms[SENSOR_ID_S8]      = load_cadence_ms("cad_s8",      CONFIG_IAQ_CADENCE_S8_MS,      &s_cadence_from_nvs[SENSOR_ID_S8]);

    TickType_t now = xTaskGetTickCount();
    for (int i = 0; i < SENSOR_ID_MAX; ++i) {
        s_schedule[i].enabled = (s_cadence_ms[i] > 0);
        s_schedule[i].period_ticks = pdMS_TO_TICKS(s_cadence_ms[i]);
        s_schedule[i].next_due = now + s_schedule[i].period_ticks;
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
                 state_to_string(old_state),
                 state_to_string(new_state));
        s_runtime[id].state = new_state;

        /* On transition to WARMING, set warm-up deadline */
        if (new_state == SENSOR_STATE_WARMING) {
            s_runtime[id].warmup_deadline_us = esp_timer_get_time() + (s_warmup_ms[id] * 1000LL);
        }

        /* On transition to READY, reset error count */
        if (new_state == SENSOR_STATE_READY) {
            s_runtime[id].error_count = 0;
        }
    }
}

/**
 * Get sensor name string for logging.
 */
static const char* sensor_id_to_string(sensor_id_t id)
{
    switch (id) {
        case SENSOR_ID_MCU:     return "MCU";
        case SENSOR_ID_SHT41:   return "SHT41";
        case SENSOR_ID_BMP280:  return "BMP280";
        case SENSOR_ID_SGP41:   return "SGP41";
        case SENSOR_ID_PMS5003: return "PMS5003";
        case SENSOR_ID_S8:      return "S8";
        default:                return "UNKNOWN";
    }
}

/**
 * Convert sensor state enum to string (internal helper).
 */
static const char* state_to_string(sensor_state_t state)
{
    switch (state) {
        case SENSOR_STATE_UNINIT:  return "UNINIT";
        case SENSOR_STATE_INIT:    return "INIT";
        case SENSOR_STATE_WARMING: return "WARMING";
        case SENSOR_STATE_READY:   return "READY";
        case SENSOR_STATE_ERROR:   return "ERROR";
        default:                   return "UNKNOWN";
    }
}


/* ===== Per-Sensor Read Handlers ===== */

static esp_err_t read_sensor_mcu(void)
{
    if (s_runtime[SENSOR_ID_MCU].state != SENSOR_STATE_READY) {
        return ESP_ERR_INVALID_STATE;
    }

    float temp_c = 0.0f;
    esp_err_t ret;

#ifdef CONFIG_IAQ_SIMULATION
    ret = sensor_sim_read_mcu_temperature(&temp_c);
#else
    ret = mcu_temp_driver_read_celsius(&temp_c);
#endif

    if (ret == ESP_OK) {
        IAQ_DATA_WITH_LOCK() {
            iaq_data_t *data = iaq_data_get();
            data->mcu_temperature = temp_c;
            data->updated_at.mcu = esp_timer_get_time();
            data->valid.mcu_temperature = true;
        }
        s_runtime[SENSOR_ID_MCU].last_read_us = esp_timer_get_time();
        s_runtime[SENSOR_ID_MCU].error_count = 0;
        xEventGroupSetBits(s_system_ctx->event_group, SENSOR_UPDATED_MCU_BIT);
        ESP_LOGD(TAG, "MCU temp: %.1f C", temp_c);
    } else {
        s_runtime[SENSOR_ID_MCU].error_count++;
        if (s_runtime[SENSOR_ID_MCU].error_count >= ERROR_THRESHOLD) {
            ESP_LOGW(TAG, "MCU sensor failed %d times, transitioning to ERROR", ERROR_THRESHOLD);
            transition_to_state(SENSOR_ID_MCU, SENSOR_STATE_ERROR);
        }
    }

    return ret;
}

/* Stub handlers for future sensors - return NOT_SUPPORTED for now */
static esp_err_t read_sensor_sht41(void) { return ESP_ERR_NOT_SUPPORTED; }
static esp_err_t read_sensor_bmp280(void) { return ESP_ERR_NOT_SUPPORTED; }
static esp_err_t read_sensor_sgp41(void) { return ESP_ERR_NOT_SUPPORTED; }
static esp_err_t read_sensor_pms5003(void) { return ESP_ERR_NOT_SUPPORTED; }

static esp_err_t read_sensor_s8(void)
{
    if (s_runtime[SENSOR_ID_S8].state != SENSOR_STATE_READY) {
        return ESP_ERR_INVALID_STATE;
    }

    float co2_ppm = 0.0f;
    esp_err_t ret;

#ifdef CONFIG_IAQ_SIMULATION
    ret = sensor_sim_read_co2(&co2_ppm);
#else
    ret = s8_driver_read_co2(&co2_ppm);
#endif

    if (ret == ESP_OK) {
        IAQ_DATA_WITH_LOCK() {
            iaq_data_t *data = iaq_data_get();
            data->co2_ppm = co2_ppm;
            data->updated_at.s8 = esp_timer_get_time();
            data->valid.co2_ppm = true;
        }
        s_runtime[SENSOR_ID_S8].last_read_us = esp_timer_get_time();
        s_runtime[SENSOR_ID_S8].error_count = 0;
        xEventGroupSetBits(s_system_ctx->event_group, SENSOR_UPDATED_S8_BIT);
        ESP_LOGD(TAG, "S8 CO2: %.0f ppm", co2_ppm);
    } else if (ret != ESP_ERR_NOT_SUPPORTED) {
        s_runtime[SENSOR_ID_S8].error_count++;
        if (s_runtime[SENSOR_ID_S8].error_count >= ERROR_THRESHOLD) {
            ESP_LOGW(TAG, "S8 sensor failed %d times, transitioning to ERROR", ERROR_THRESHOLD);
            transition_to_state(SENSOR_ID_S8, SENSOR_STATE_ERROR);
        }
    }

    return ret;
}

/**
 * Sensor coordinator task.
 * Manages sensor state machine, warm-up periods, and periodic reads.
 */
static void sensor_coordinator_task(void *arg)
{
    ESP_LOGI(TAG, "Sensor coordinator task started");

    /* Brief delay for hardware stabilization */
    vTaskDelay(pdMS_TO_TICKS(500));

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

    xEventGroupSetBits(s_system_ctx->event_group, SENSORS_READY_BIT);

    while (s_running) {
        int64_t now_us = esp_timer_get_time();

        /* Check warm-up deadlines, promote WARMING -> READY */
        for (int i = 0; i < SENSOR_ID_MAX; ++i) {
            if (s_runtime[i].state == SENSOR_STATE_WARMING) {
                if (now_us >= s_runtime[i].warmup_deadline_us) {
                    ESP_LOGI(TAG, "%s warm-up complete", sensor_id_to_string(i));
                    transition_to_state(i, SENSOR_STATE_READY);
                }
            }
        }

        /* Update global warming_up flag */
        bool any_warming = false;
        for (int i = 0; i < SENSOR_ID_MAX; ++i) {
            if (s_runtime[i].state == SENSOR_STATE_WARMING) {
                any_warming = true;
                break;
            }
        }
        IAQ_DATA_WITH_LOCK() {
            iaq_data_get()->warming_up = any_warming;
        }
        /* Calculate time until next sensor is due */
        TickType_t now = xTaskGetTickCount();
        TickType_t next_wake = portMAX_DELAY;

        for (int i = 0; i < SENSOR_ID_MAX; ++i) {
            if (s_schedule[i].enabled) {
                TickType_t time_until_due = s_schedule[i].next_due - now;
                /* Handle tick overflow and past-due sensors */
                if ((int32_t)time_until_due <= 0) {
                    next_wake = 0;  /* Sensor is due now */
                    break;
                } else if (time_until_due < next_wake) {
                    next_wake = time_until_due;
                }
            }
        }

        /* Handle pending coordinator commands with calculated timeout */
        sensor_cmd_t cmd;
        /* Ensure minimum 1-tick delay to prevent tight loop when next_wake=0 */
        TickType_t queue_timeout = (next_wake == 0) ? 1 : next_wake;
        if (s_cmd_queue && xQueueReceive(s_cmd_queue, &cmd, queue_timeout) == pdTRUE) {
            esp_err_t op_res = ESP_ERR_NOT_SUPPORTED;
            switch (cmd.type) {
                case CMD_READ:
                    /* Dispatch to per-sensor read handler */
                    switch (cmd.id) {
                        case SENSOR_ID_MCU:     op_res = read_sensor_mcu(); break;
                        case SENSOR_ID_SHT41:   op_res = read_sensor_sht41(); break;
                        case SENSOR_ID_BMP280:  op_res = read_sensor_bmp280(); break;
                        case SENSOR_ID_SGP41:   op_res = read_sensor_sgp41(); break;
                        case SENSOR_ID_PMS5003: op_res = read_sensor_pms5003(); break;
                        case SENSOR_ID_S8:      op_res = read_sensor_s8(); break;
                        default: op_res = ESP_ERR_INVALID_ARG; break;
                    }
                    break;
                case CMD_RESET:
                    /* Attempt to recover ERROR state sensors */
                    if (cmd.id == SENSOR_ID_MCU) {
                        mcu_temp_driver_disable();
                        if (mcu_temp_driver_enable() == ESP_OK) {
                            transition_to_state(SENSOR_ID_MCU, SENSOR_STATE_READY);
                            op_res = ESP_OK;
                        } else {
                            op_res = ESP_FAIL;
                        }
                    } else if (cmd.id == SENSOR_ID_S8) {
                        op_res = s8_driver_reset();
                        if (op_res == ESP_OK) {
                            transition_to_state(SENSOR_ID_S8, SENSOR_STATE_READY);
                        }
                    }
                    break;
                case CMD_CALIBRATE:
                    if (cmd.id == SENSOR_ID_S8) {
                        op_res = s8_driver_calibrate_co2(cmd.value);
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
            /* Only read if sensor is READY, enabled, and due */
            if (s_runtime[i].state == SENSOR_STATE_READY &&
                s_schedule[i].enabled &&
                (int32_t)(now - s_schedule[i].next_due) >= 0) {

                /* Dispatch to appropriate read handler */
                esp_err_t read_res = ESP_ERR_NOT_SUPPORTED;
                switch (i) {
                    case SENSOR_ID_MCU:     read_res = read_sensor_mcu(); break;
                    case SENSOR_ID_SHT41:   read_res = read_sensor_sht41(); break;
                    case SENSOR_ID_BMP280:  read_res = read_sensor_bmp280(); break;
                    case SENSOR_ID_SGP41:   read_res = read_sensor_sgp41(); break;
                    case SENSOR_ID_PMS5003: read_res = read_sensor_pms5003(); break;
                    case SENSOR_ID_S8:      read_res = read_sensor_s8(); break;
                    default: break;
                }

                /* On successful read, signal DATA_READY */
                if (read_res == ESP_OK) {
                    xEventGroupSetBits(s_system_ctx->event_group, SENSORS_DATA_READY_BIT);
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
    }

    /* Create command queue */
    s_cmd_queue = xQueueCreate(8, sizeof(sensor_cmd_t));
    if (s_cmd_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create sensor command queue");
        return ESP_FAIL;
    }

#ifdef CONFIG_IAQ_SIMULATION
    ESP_LOGW(TAG, "*** SIMULATION MODE ENABLED - Using fake sensor data ***");
#endif

    /* Initialize I2C bus for SHT41, BMP280, SGP41 */
    esp_err_t ret = i2c_bus_init();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "I2C bus init failed: %s (I2C sensors disabled)", esp_err_to_name(ret));
        transition_to_state(SENSOR_ID_SHT41, SENSOR_STATE_ERROR);
        transition_to_state(SENSOR_ID_BMP280, SENSOR_STATE_ERROR);
        transition_to_state(SENSOR_ID_SGP41, SENSOR_STATE_ERROR);
    } else {
        ESP_LOGI(TAG, "I2C bus initialized successfully");
        i2c_bus_probe();  /* Log detected devices */
        /* I2C sensors will be initialized in task startup */
    }

    /* Initialize UART for PMS5003 */
    ret = uart_bus_init(CONFIG_IAQ_PMS5003_UART_PORT,
                        CONFIG_IAQ_PMS5003_TX_GPIO,
                        CONFIG_IAQ_PMS5003_RX_GPIO,
                        9600,
                        CONFIG_IAQ_PMS5003_RX_BUF_SIZE);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "PMS5003 UART init failed: %s", esp_err_to_name(ret));
        transition_to_state(SENSOR_ID_PMS5003, SENSOR_STATE_ERROR);
    } else {
        ESP_LOGI(TAG, "PMS5003 UART initialized");
    }

    /* Initialize S8 driver (includes UART init) */
    ret = s8_driver_init();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "S8 driver init failed: %s", esp_err_to_name(ret));
        transition_to_state(SENSOR_ID_S8, SENSOR_STATE_ERROR);
    } else {
        ESP_LOGI(TAG, "S8 driver initialized");
        transition_to_state(SENSOR_ID_S8, SENSOR_STATE_INIT);
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

    /* For now, just mark as initialized */
    s_initialized = true;

    ESP_LOGI(TAG, "Sensor coordinator initialized (stub - no sensors yet)");
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

    /* Wait for task to finish */
    vTaskDelay(pdMS_TO_TICKS(100));

    /* Deinitialize sensors */
    if (s_runtime[SENSOR_ID_MCU].state != SENSOR_STATE_UNINIT) {
        mcu_temp_driver_disable();
        mcu_temp_driver_deinit();
        transition_to_state(SENSOR_ID_MCU, SENSOR_STATE_UNINIT);
    }

    if (s_runtime[SENSOR_ID_S8].state != SENSOR_STATE_UNINIT) {
        s8_driver_deinit();
        transition_to_state(SENSOR_ID_S8, SENSOR_STATE_UNINIT);
    }

    /* De-initialize buses */
    i2c_bus_deinit();
    uart_bus_deinit(CONFIG_IAQ_PMS5003_UART_PORT);
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
    return enqueue_cmd(CMD_CALIBRATE, id, value, NULL);
}

esp_err_t sensor_coordinator_set_cadence(sensor_id_t id, uint32_t interval_ms)
{
    if (id < 0 || id >= SENSOR_ID_MAX) return ESP_ERR_INVALID_ARG;
    if (!s_initialized) return ESP_ERR_INVALID_STATE;
    s_schedule[id].enabled = (interval_ms > 0);
    s_schedule[id].period_ticks = pdMS_TO_TICKS(interval_ms);
    s_schedule[id].next_due = xTaskGetTickCount() + s_schedule[id].period_ticks;
    s_cadence_ms[id] = interval_ms;
    s_cadence_from_nvs[id] = true; /* persisted */
    switch (id) {
        case SENSOR_ID_MCU:      save_cadence_ms("cad_mcu", interval_ms); break;
        case SENSOR_ID_SHT41:    save_cadence_ms("cad_sht41", interval_ms); break;
        case SENSOR_ID_BMP280:   save_cadence_ms("cad_bmp280", interval_ms); break;
        case SENSOR_ID_SGP41:    save_cadence_ms("cad_sgp41", interval_ms); break;
        case SENSOR_ID_PMS5003:  save_cadence_ms("cad_pms5003", interval_ms); break;
        case SENSOR_ID_S8:       save_cadence_ms("cad_s8", interval_ms); break;
        default: break;
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

    /* Copy runtime state (safe without lock as reads are atomic on this platform) */
    out_info->state = s_runtime[id].state;
    out_info->warmup_deadline_us = s_runtime[id].warmup_deadline_us;
    out_info->last_read_us = s_runtime[id].last_read_us;
    out_info->error_count = s_runtime[id].error_count;

    return ESP_OK;
}

const char* sensor_coordinator_state_to_string(sensor_state_t state)
{
    switch (state) {
        case SENSOR_STATE_UNINIT:  return "UNINIT";
        case SENSOR_STATE_INIT:    return "INIT";
        case SENSOR_STATE_WARMING: return "WARMING";
        case SENSOR_STATE_READY:   return "READY";
        case SENSOR_STATE_ERROR:   return "ERROR";
        default:                   return "UNKNOWN";
    }
}

