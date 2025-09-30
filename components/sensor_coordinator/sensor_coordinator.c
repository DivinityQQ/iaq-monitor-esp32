/* components/sensor_coordinator/sensor_coordinator.c */
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "mcu_temp_driver.h"
#include "s8_driver.h"

#include "sensor_coordinator.h"
#include "iaq_data.h"
#include "iaq_config.h"

static const char *TAG = "SENSOR_COORD";

extern EventGroupHandle_t g_event_group;  /* From main */

static TaskHandle_t s_sensor_task_handle = NULL;
static bool s_initialized = false;
static bool s_running = false;
static bool s_temp_inited = false;


typedef enum { CMD_READ = 0, CMD_RESET, CMD_CALIBRATE } sensor_cmd_type_t;
typedef struct {
    sensor_cmd_type_t type;
    sensor_id_t id;
    int value;
    QueueHandle_t resp_queue; /* optional: where to send esp_err_t result */
} sensor_cmd_t;

static QueueHandle_t s_cmd_queue = NULL;


/**
 * Sensor coordinator task.
 * This is a stub implementation that will be filled in when sensors are added.
 */
static void sensor_coordinator_task(void *arg)
{
    ESP_LOGI(TAG, "Sensor coordinator task started");

    /* Wait a bit for system to stabilize */
    vTaskDelay(pdMS_TO_TICKS(2000));

    xEventGroupSetBits(g_event_group, SENSORS_READY_BIT);

    TickType_t last_wake = xTaskGetTickCount();

    while (s_running) {
        /* Handle pending coordinator commands */
        sensor_cmd_t cmd;
        while (s_cmd_queue && xQueueReceive(s_cmd_queue, &cmd, 0) == pdTRUE) {
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
                                data->last_update = esp_timer_get_time() / 1000000;
                                data->warming_up = false;
                            }
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
        }
        /* TODO: Phase 1: Read environmental baseline (SHT41, BMP280) */
        /* TODO: Phase 2: Apply pre-compensation (SGP41) */
        /* TODO: Phase 3: Read gas/particle sensors (S8, PMS5003, SGP41) */
        /* TODO: Phase 4: Apply post-compensation algorithms */

        /* Read internal MCU temperature if available */
        float mcu_temp_c = 0.0f;
        if (s_temp_inited) {
            if (mcu_temp_driver_read_celsius(&mcu_temp_c) != ESP_OK) {
                mcu_temp_c = 0.0f;
            }
        }

        IAQ_DATA_WITH_LOCK() {
            iaq_data_t *data = iaq_data_get();

            /* Update timestamp */
            data->last_update = esp_timer_get_time() / 1000000;

            /* Mark system as operational */
            data->warming_up = false;

            /* Update MCU temperature reading */
            data->mcu_temperature = mcu_temp_c;

            /* TODO: Set actual sensor values here */
            /* For now, leave them at 0 */
            
        }

        /* Signal that data is ready */
        /* Data ready signaled on successful reads */

        ESP_LOGD(TAG, "Sensor reading cycle complete");

        /* Sleep until next reading */
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(SENSOR_READING_INTERVAL_MS));
    }

    ESP_LOGI(TAG, "Sensor coordinator task stopped");
    s_sensor_task_handle = NULL;
    vTaskDelete(NULL);
}

esp_err_t sensor_coordinator_init(void)
{
    if (s_initialized) {
        ESP_LOGW(TAG, "Sensor coordinator already initialized");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Initializing sensor coordinator");

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
    // Placeholder: per-sensor cadence scheduling will be added; return not supported for now
    (void)id; (void)interval_ms;
    return ESP_ERR_NOT_SUPPORTED;
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

