/* main/main.c */
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_timer.h"
#include "esp_console.h"
#include "esp_chip_info.h"
#include "driver/gpio.h"

#include "iaq_config.h"
#include "wifi_manager.h"
#include "mqtt_manager.h"

static const char *TAG = "IAQ_MAIN";

/* Global system info */
system_info_t g_system_info = {
    .state = SYSTEM_STATE_INIT,
    .wifi_connected = false,
    .mqtt_connected = false,
};

/* Event group for task synchronization */
EventGroupHandle_t g_event_group;

/* Timer for periodic status */
static esp_timer_handle_t status_timer;

/* Function prototypes */
static void status_timer_callback(void* arg);
static void init_console(void);
static void register_commands(void);
static void blink_led(int count);

/* Status timer callback - runs every 30 seconds */
static void status_timer_callback(void* arg)
{
    g_system_info.uptime_seconds = esp_timer_get_time() / 1000000;
    g_system_info.free_heap = esp_get_free_heap_size();
    g_system_info.min_free_heap = esp_get_minimum_free_heap_size();
    
    ESP_LOGI(TAG, "Status: Uptime=%lds, Free heap=%ld, Min heap=%ld, WiFi=%s, MQTT=%s",
             g_system_info.uptime_seconds,
             g_system_info.free_heap,
             g_system_info.min_free_heap,
             g_system_info.wifi_connected ? "Connected" : "Disconnected",
             g_system_info.mqtt_connected ? "Connected" : "Disconnected");
    
    /* Publish status if MQTT is connected */
    if (g_system_info.mqtt_connected) {
        mqtt_publish_status(&g_system_info);
    }
}

/* Simple LED blink for status indication */
static void blink_led(int count)
{
    gpio_set_direction(CONFIG_IAQ_STATUS_LED_GPIO, GPIO_MODE_OUTPUT);
    for (int i = 0; i < count; i++) {
        gpio_set_level(CONFIG_IAQ_STATUS_LED_GPIO, 1);
        vTaskDelay(pdMS_TO_TICKS(100));
        gpio_set_level(CONFIG_IAQ_STATUS_LED_GPIO, 0);
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

/* Console command: status */
static int cmd_status(int argc, char **argv)
{
    printf("\n=== IAQ Monitor Status ===\n");
    printf("Version: %d.%d.%d\n", IAQ_VERSION_MAJOR, IAQ_VERSION_MINOR, IAQ_VERSION_PATCH);
    printf("Uptime: %ld seconds\n", g_system_info.uptime_seconds);
    printf("Free heap: %ld bytes\n", g_system_info.free_heap);
    printf("Min free heap: %ld bytes\n", g_system_info.min_free_heap);
    printf("WiFi: %s", g_system_info.wifi_connected ? "Connected" : "Disconnected");
    if (g_system_info.wifi_connected) {
        printf(" (RSSI: %ld dBm)", g_system_info.wifi_rssi);
    }
    printf("\n");
    printf("MQTT: %s\n", g_system_info.mqtt_connected ? "Connected" : "Disconnected");
    
    /* Future: Add sensor readings here */
    printf("\n=== Sensor Data ===\n");
    printf("Temperature: %.1f°C\n", g_system_info.sensors.temperature);
    printf("Humidity: %.1f%%\n", g_system_info.sensors.humidity);
    printf("CO2: %.0f ppm\n", g_system_info.sensors.co2);
    printf("PM2.5: %.1f µg/m³\n", g_system_info.sensors.pm2_5);
    
    return 0;
}

/* Console command: restart */
static int cmd_restart(int argc, char **argv)
{
    printf("Restarting in 3 seconds...\n");
    vTaskDelay(pdMS_TO_TICKS(3000));
    esp_restart();
    return 0;
}

/* Initialize console for runtime commands */
static void init_console(void)
{
#if CONFIG_IAQ_ENABLE_CONSOLE_COMMANDS
    esp_console_repl_t *repl = NULL;
    esp_console_repl_config_t repl_config = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    
    repl_config.prompt = "iaq>";
    repl_config.max_cmdline_length = 256;

    /* Register commands */
    esp_console_register_help_command();
    
    const esp_console_cmd_t status_cmd = {
        .command = "status",
        .help = "Show system status",
        .hint = NULL,
        .func = &cmd_status,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&status_cmd));
    
    const esp_console_cmd_t restart_cmd = {
        .command = "restart",
        .help = "Restart the system",
        .hint = NULL,
        .func = &cmd_restart,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&restart_cmd));
    
    /* Start console */
    esp_console_dev_uart_config_t hw_config = ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_console_new_repl_uart(&hw_config, &repl_config, &repl));
    ESP_ERROR_CHECK(esp_console_start_repl(repl));
    
    ESP_LOGI(TAG, "Console initialized. Press Enter to activate.");
#endif
}

/* Main application entry point */
void app_main(void)
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
    
    ESP_LOGI(TAG, "Free heap: %ld bytes", esp_get_free_heap_size());
    
    /* Initialize NVS */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    
    /* Initialize networking */
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    
    /* Create event group for synchronization */
    g_event_group = xEventGroupCreate();
    
    /* Initialize status LED */
    blink_led(3);  /* 3 blinks = starting */
    
    /* Initialize WiFi */
    ESP_LOGI(TAG, "Initializing WiFi...");
    ESP_ERROR_CHECK(wifi_manager_init());
    ESP_ERROR_CHECK(wifi_manager_start());
    
    /* Wait for WiFi connection */
    EventBits_t bits = xEventGroupWaitBits(g_event_group,
                                           WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                           pdFALSE, pdFALSE,
                                           portMAX_DELAY);
    
    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "WiFi connected successfully");
        g_system_info.wifi_connected = true;
        g_system_info.state = SYSTEM_STATE_RUNNING;
        blink_led(2);  /* 2 blinks = WiFi connected */
        
        /* Initialize MQTT */
        ESP_LOGI(TAG, "Initializing MQTT...");
        ESP_ERROR_CHECK(mqtt_manager_init());
        ESP_ERROR_CHECK(mqtt_manager_start());
        
        /* Wait a bit for MQTT to connect */
        bits = xEventGroupWaitBits(g_event_group,
                                   MQTT_CONNECTED_BIT,
                                   pdFALSE, pdFALSE,
                                   pdMS_TO_TICKS(10000));
        
        if (bits & MQTT_CONNECTED_BIT) {
            ESP_LOGI(TAG, "MQTT connected successfully");
            g_system_info.mqtt_connected = true;
            blink_led(1);  /* 1 blink = fully connected */
        }
    } else {
        ESP_LOGW(TAG, "WiFi connection failed");
        g_system_info.state = SYSTEM_STATE_ERROR;
        /* System will continue running, WiFi manager will retry */
    }
    
    /* Create status timer */
    const esp_timer_create_args_t status_timer_args = {
        .callback = &status_timer_callback,
        .name = "status_timer"
    };
    ESP_ERROR_CHECK(esp_timer_create(&status_timer_args, &status_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(status_timer, 30 * 1000000));  /* 30 seconds */
    
    /* Initialize console */
    init_console();
    
    ESP_LOGI(TAG, "System initialization complete");
    ESP_LOGI(TAG, "Type 'help' for available commands");
    
    /* Main loop - in the future, this could monitor system health */
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10000));  /* Sleep 10 seconds */
        
        /* Future: Check sensor health, trigger readings, etc. */
    }
}