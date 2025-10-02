/* components/sensor_coordinator/sensor_coordinator.c */
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "mcu_temp_driver.h"
#include "s8_driver.h"
#include "nvs_flash.h"
#include "nvs.h"

#include "sensor_coordinator.h"
#include "iaq_data.h"
#include "iaq_config.h"

static const char *TAG = "SENSOR_COORD";

static iaq_system_context_t *s_system_ctx = NULL;
static TaskHandle_t s_sensor_task_handle = NULL;
static volatile bool s_initialized = false;
static volatile bool s_running = false;
static volatile bool s_temp_inited = false;


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
 * Sensor coordinator task.
 * This is a stub implementation that will be filled in when sensors are added.
 */
static void sensor_coordinator_task(void *arg)
{
    ESP_LOGI(TAG, "Sensor coordinator task started");

    /* Brief delay for hardware stabilization */
    vTaskDelay(pdMS_TO_TICKS(500));

    xEventGroupSetBits(s_system_ctx->event_group, SENSORS_READY_BIT);

    while (s_running) {
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
        if (s_cmd_queue && xQueueReceive(s_cmd_queue, &cmd, next_wake) == pdTRUE) {
            esp_err_t op_res = ESP_ERR_NOT_SUPPORTED;
            switch (cmd.type) {
                case CMD_READ:
                    if (cmd.id == SENSOR_ID_MCU && s_temp_inited) {
                        float t = 0.0f;
                        op_res = mcu_temp_driver_read_celsius(&t);
                        if (op_res == ESP_OK) {
                            IAQ_DATA_WITH_LOCK() {
                                iaq_data_t *data = iaq_data_get();
                                data->mcu_temperature = t;
                                data->updated_at.mcu = esp_timer_get_time() / 1000000;
                                data->warming_up = false;
                            }
                            /* Signal MCU sensor update */
                            xEventGroupSetBits(s_system_ctx->event_group, SENSOR_UPDATED_MCU_BIT);
                            /* Data ready signaled on successful reads */
                        } else {
                            // keep op_res as error
                        }
                    }
                    break;
                case CMD_RESET:
                    if (cmd.id == SENSOR_ID_MCU && s_temp_inited) {
                        (void)mcu_temp_driver_disable();
                        op_res = mcu_temp_driver_enable();
                    }
                    break;
                case CMD_CALIBRATE:
                    if (cmd.id == SENSOR_ID_S8) {
                        op_res = s8_calibrate_co2(cmd.value);
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

        /* Periodic scheduler per sensor */
        now = xTaskGetTickCount();
        if (s_temp_inited && s_schedule[SENSOR_ID_MCU].enabled && (int32_t)(now - s_schedule[SENSOR_ID_MCU].next_due) >= 0) {
            float t = 0.0f;
            if (mcu_temp_driver_read_celsius(&t) == ESP_OK) {
                IAQ_DATA_WITH_LOCK() {
                    iaq_data_t *data = iaq_data_get();
                    data->mcu_temperature = t;
                    data->updated_at.mcu = esp_timer_get_time() / 1000000;
                    data->warming_up = false;
                }
                xEventGroupSetBits(s_system_ctx->event_group, SENSORS_DATA_READY_BIT | SENSOR_UPDATED_MCU_BIT);
            }
            s_schedule[SENSOR_ID_MCU].next_due = now + s_schedule[SENSOR_ID_MCU].period_ticks;
        }

        /* TODO: add other sensors here in dependency-aware order */
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

    /* Create command queue */
    s_cmd_queue = xQueueCreate(8, sizeof(sensor_cmd_t));
    if (s_cmd_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create sensor command queue");
        return ESP_FAIL;
    }
    /* TODO: Initialize I2C bus for SHT41, BMP280, SGP41 */
    /* TODO: Initialize UART for PMS5003 and Senseair S8 */
    /* TODO: Initialize GPIO for sensor power control */

    /* Initialize internal temperature sensor via driver */
    if (mcu_temp_driver_init(-10, 80) == ESP_OK) {
        if (mcu_temp_driver_enable() == ESP_OK) {
            s_temp_inited = true;
            ESP_LOGI(TAG, "MCU temperature sensor enabled");
        } else {
            ESP_LOGW(TAG, "Failed to enable MCU temperature sensor");
        }
    } else {
        ESP_LOGW(TAG, "Failed to initialize MCU temperature sensor");
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

    if (s_temp_inited) {
        mcu_temp_driver_disable();
        mcu_temp_driver_deinit();
        s_temp_inited = false;
    }

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

