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
#include "web_portal.h"
#include "web_console.h"
#include "pm_guard.h"
#include "power_board.h"
#include "ota_manager.h"

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

static void ota_validation_task(void *arg)
{
    (void)arg;
    const uint64_t timeout_us = (uint64_t)CONFIG_IAQ_OTA_VALIDATION_TIMEOUT_MIN * 60ULL * 1000000ULL;
    const uint64_t start_us = esp_timer_get_time();
    while (1) {
        bool wifi_ok = wifi_manager_is_connected() || wifi_manager_is_ap_active();
        bool sensors_ok = sensor_coordinator_any_ready();
        bool web_ok = web_portal_is_running();
        if (wifi_ok && sensors_ok && web_ok) {
            esp_err_t r = ota_manager_mark_valid();
            if (r == ESP_OK) {
                ESP_LOGI(TAG, "OTA self-test passed, firmware marked valid");
                break;
            }
        }
        if ((esp_timer_get_time() - start_us) >= timeout_us) {
            esp_err_t r = ota_manager_mark_valid();
            ESP_LOGW(TAG, "OTA self-test timeout reached; marking firmware valid (r=%s)", esp_err_to_name(r));
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    vTaskDelete(NULL);
}

static void start_ota_validation_if_needed(void)
{
    ota_runtime_info_t info = {0};
    if (ota_manager_get_runtime(&info) != ESP_OK) return;
    if (!info.pending_verify) return;

    ESP_LOGW(TAG, "Pending firmware verification detected; starting validation task");
    BaseType_t rc = xTaskCreatePinnedToCore(ota_validation_task, "ota_valid",
                                            TASK_STACK_OTA_VALIDATION, NULL,
                                            TASK_PRIORITY_OTA_VALIDATION, NULL,
                                            TASK_CORE_OTA_VALIDATION);
    if (rc != pdPASS) {
        ESP_LOGE(TAG, "Failed to start OTA validation task");
    }
}

/**
 * Main application entry point.
 * Initializes all components and lets them work independently.
 */
void app_main(void)
{
    /* Initialize core system (NVS, networking, event group) */
    ESP_ERROR_CHECK(init_core_system());

    /* Configure runtime PM (DFS + light sleep) and create shared locks */
#ifdef CONFIG_IAQ_PM_RUNTIME_ENABLE
    ESP_ERROR_CHECK(pm_guard_init());
#else
    ESP_LOGW(TAG, "Runtime PM disabled via CONFIG_IAQ_PM_RUNTIME_ENABLE");
#endif

    /* Initialize IAQ data structure */
    ESP_ERROR_CHECK(iaq_data_init());

    /* Initialize OTA manager (handles OTA state + pending verify bookkeeping) */
    ESP_ERROR_CHECK(ota_manager_init());

    /* Initialize PowerFeather board integration (fail-soft if disabled or absent) */
    esp_err_t pf_ret = power_board_init();
    if (pf_ret == ESP_OK) {
        ESP_LOGI(TAG, "PowerFeather integration enabled");
    } else if (pf_ret == ESP_ERR_NOT_SUPPORTED) {
        ESP_LOGI(TAG, "PowerFeather integration not active (disabled or not detected)");
    } else {
        ESP_ERROR_CHECK(pf_ret);
    }

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

    /* Initialize web portal (mount LittleFS, prepare handlers) */
    ESP_LOGI(TAG, "Initializing web portal");
    ESP_ERROR_CHECK(web_portal_init(&g_system_ctx));

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

#if CONFIG_IAQ_WEB_CONSOLE_ENABLE
    esp_err_t wc_err = web_console_init();
    if (wc_err != ESP_OK) {
        ESP_LOGW(TAG, "Web console init failed: %s", esp_err_to_name(wc_err));
    }
#endif

    /* Start web portal after Wi‑Fi begin to make protocol choice simpler.
     * It will start HTTP by default and switch to HTTPS once STA connects. */
    ESP_ERROR_CHECK(web_portal_start());

    /* If bootloader marked this image for verification, validate once services are healthy */
    start_ota_validation_if_needed();

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
