/* main/main.c */
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_chip_info.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_event.h"

#include "iaq_config.h"
#include "iaq_data.h"
#include "system_context.h"
#include "wifi_manager.h"
#include "mqtt_manager.h"
#include "sensor_coordinator.h"
#include "console_commands.h"
#include "time_sync.h"
#include "display_oled/display_ui.h"
#include "iaq_profiler.h"

static const char *TAG = "IAQ_MAIN";

/* System context for inter-component coordination */
static iaq_system_context_t g_system_ctx;

/* Timer for periodic system status updates (console logging + metrics) */
static esp_timer_handle_t system_status_timer;

/* Reduce noise from lower-level Wi-Fi libraries while keeping our INFO logs. */
static void configure_log_levels(void)
{
    /* Tags observed in Wi-Fi/PHY bring-up that are chatty at INFO */
    esp_log_level_set("wifi",      ESP_LOG_WARN);
    esp_log_level_set("wifi_init", ESP_LOG_WARN);
    esp_log_level_set("net80211",  ESP_LOG_WARN);
    esp_log_level_set("pp",        ESP_LOG_WARN);
    esp_log_level_set("phy_init",  ESP_LOG_WARN);
}

/**
 * System status timer callback - updates system metrics and logs to console.
 * Runs every 30 seconds, independent of MQTT connectivity.
 */
static void system_status_timer_callback(void* arg)
{
    (void)arg;

    /* Read values that don't require the data lock to minimize hold time */
    int32_t rssi = wifi_manager_get_rssi();

    /* Update system metrics in IAQ data */
    IAQ_DATA_WITH_LOCK() {
        iaq_data_t *data = iaq_data_get();
        data->system.uptime_seconds = esp_timer_get_time() / 1000000;
        data->system.free_heap = esp_get_free_heap_size();
        data->system.min_free_heap = esp_get_minimum_free_heap_size();
        data->system.wifi_rssi = rssi;
    }

    /* Unified status/profiling report (simple when profiling disabled) */
    iaq_status_report();
}

/**
 * Event handler for custom IAQ events.
 * Handles WiFi connectivity changes and manages MQTT lifecycle.
 */
static void iaq_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_base == IAQ_EVENT) {
        switch (event_id) {
            case IAQ_EVENT_WIFI_CONNECTED:
                ESP_LOGI(TAG, "WiFi connected event received");

                /* Start MQTT if configured and not already connected */
                if (mqtt_manager_is_configured() && !mqtt_manager_is_connected()) {
                    ESP_LOGI(TAG, "WiFi connected, starting MQTT");
                    mqtt_manager_start();
                }
                break;

            case IAQ_EVENT_WIFI_DISCONNECTED:
                ESP_LOGD(TAG, "WiFi disconnected event received");
                break;

            default:
                break;
        }
    }
}

/**
 * Initialize core system components.
 */
static esp_err_t init_core_system(void)
{
    ESP_LOGI(TAG, "=== IAQ Monitor v%d.%d.%d Starting ===",
             IAQ_VERSION_MAJOR, IAQ_VERSION_MINOR, IAQ_VERSION_PATCH);

    /* Print chip information */
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);
    ESP_LOGI(TAG, "ESP32-%s, %d CPU cores, WiFi%s%s, Silicon rev %d",
             CONFIG_IDF_TARGET,
             chip_info.cores,
             (chip_info.features & CHIP_FEATURE_BT) ? "/BT" : "",
             (chip_info.features & CHIP_FEATURE_BLE) ? "/BLE" : "",
             chip_info.revision);

    /* Quiet noisy subsystem logs (Wi‑Fi stack) */
    configure_log_levels();

    ESP_LOGI(TAG, "Free heap: %lu bytes", esp_get_free_heap_size());

    /* Initialize NVS */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    /* Initialize networking stack */
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    /* Initialize system context (event group, etc.) */
    ret = iaq_system_context_init(&g_system_ctx);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize system context");
        return ret;
    }

    /* Initialize time sync (SNTP/TZ) */
    ESP_ERROR_CHECK(time_sync_init(&g_system_ctx));

    ESP_LOGI(TAG, "Core system initialized");
    return ESP_OK;
}

/**
 * Main application entry point.
 * Initializes all components and lets them work independently.
 */
void app_main(void)
{
    /* Initialize core system (NVS, networking, event group) */
    ESP_ERROR_CHECK(init_core_system());

    /* Initialize IAQ data structure */
    ESP_LOGI(TAG, "Initializing IAQ data structure");
    ESP_ERROR_CHECK(iaq_data_init());

    /* Initialize profiler (no-op when disabled) */
    iaq_profiler_init();

    /* Create and start system status timer BEFORE mqtt_manager_init to prevent race */
    ESP_LOGI(TAG, "Creating system status timer");
    const esp_timer_create_args_t system_status_timer_args = {
        .callback = &system_status_timer_callback,
        .name = "system_status"
    };
    ESP_ERROR_CHECK(esp_timer_create(&system_status_timer_args, &system_status_timer));
    uint64_t status_interval_ms = STATUS_PUBLISH_INTERVAL_MS;
#ifdef CONFIG_IAQ_PROFILING
    if (CONFIG_IAQ_PROFILING && CONFIG_IAQ_PROFILING_INTERVAL_SEC > 0) {
        status_interval_ms = (uint64_t)CONFIG_IAQ_PROFILING_INTERVAL_SEC * 1000ULL;
    }
#endif
    ESP_ERROR_CHECK(esp_timer_start_periodic(system_status_timer, status_interval_ms * 1000ULL));

    /* Call timer callback once immediately to populate initial values before MQTT init */
    system_status_timer_callback(NULL);
    ESP_LOGI(TAG, "System status timer started (%llu ms interval)", (unsigned long long)status_interval_ms);

    /* Initialize WiFi manager */
    ESP_LOGI(TAG, "Initializing WiFi manager");
    ESP_ERROR_CHECK(wifi_manager_init(&g_system_ctx));

    /* Initialize MQTT manager (system metrics already populated, no race condition) */
    ESP_LOGI(TAG, "Initializing MQTT manager");
    ESP_ERROR_CHECK(mqtt_manager_init(&g_system_ctx));

    /* Initialize sensor coordinator */
    ESP_LOGI(TAG, "Initializing sensor coordinator");
    ESP_ERROR_CHECK(sensor_coordinator_init(&g_system_ctx));

    /* Initialize console commands */
    ESP_LOGI(TAG, "Initializing console commands");
    ESP_ERROR_CHECK(console_commands_init());

    /* Register event handler for IAQ custom events */
    ESP_LOGI(TAG, "Registering IAQ event handler");
    ESP_ERROR_CHECK(esp_event_handler_register(IAQ_EVENT, ESP_EVENT_ANY_ID,
                                                &iaq_event_handler, NULL));

    /* Initialize and start OLED UI (if enabled) */
    ESP_LOGI(TAG, "Initializing display UI");
    ESP_ERROR_CHECK(display_ui_init(&g_system_ctx));
    ESP_ERROR_CHECK(display_ui_start());

    /* Start sensor coordinator task (begin warm-up immediately) */
    ESP_LOGD(TAG, "Starting sensor coordinator");
    ESP_ERROR_CHECK(sensor_coordinator_start());

    /* Start WiFi (non-blocking, event-driven) */
    ESP_LOGI(TAG, "Starting WiFi");
    ESP_ERROR_CHECK(wifi_manager_start());
    if (wifi_manager_is_provisioned()) {
        ESP_LOGI(TAG, "WiFi provisioned, connecting in background");
    } else {
        ESP_LOGW(TAG, "WiFi not provisioned. SoftAP '%s' is active for setup.", CONFIG_IAQ_AP_SSID);
        ESP_LOGW(TAG, "You can also use console: wifi set <ssid> <password> and then wifi restart");
    }

    /* MQTT will be started automatically by iaq_event_handler when WiFi connects */
    if (mqtt_manager_is_configured()) {
        ESP_LOGI(TAG, "MQTT configured, will connect when WiFi is ready");
    } else {
        ESP_LOGW(TAG, "MQTT not configured. Use console: mqtt set <broker_url> [user] [pass]");
    }

    ESP_LOGI(TAG, "=== System initialization complete ===");
    ESP_LOGI(TAG, "All components running independently");
    ESP_LOGI(TAG, "Type 'help' in console for available commands");

    /* Main task can now just monitor system health */
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10000));  /* Sleep 10 seconds */

        /* Future: Add system health monitoring, watchdog feeding, etc. */
    }
}
