/* components/console_commands/console_commands.c */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_console.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_chip_info.h"
#include "esp_flash.h"

#include "console_commands.h"
#include "iaq_data.h"
#include "iaq_config.h"
#include "wifi_manager.h"
#include "mqtt_manager.h"
#include "sensor_coordinator.h"

static const char *TAG = "CONSOLE_CMD";

/* ==================== STATUS COMMAND ==================== */
static int cmd_status(int argc, char **argv)
{
    printf("\n=== IAQ Monitor Status ===\n");
    printf("Version: %d.%d.%d\n", IAQ_VERSION_MAJOR, IAQ_VERSION_MINOR, IAQ_VERSION_PATCH);

    IAQ_DATA_WITH_LOCK() {
        iaq_data_t *data = iaq_data_get();

        /* System Status */
        printf("\n--- System ---\n");
        printf("Uptime: %lu seconds\n", data->system.uptime_seconds);
        printf("Free heap: %lu bytes\n", data->system.free_heap);
        printf("Min free heap: %lu bytes\n", data->system.min_free_heap);

        /* Network Status */
        printf("\n--- Network ---\n");
        printf("WiFi: %s", data->system.wifi_connected ? "Connected" : "Disconnected");
        if (data->system.wifi_connected) {
            printf(" (RSSI: %ld dBm)", data->system.wifi_rssi);
        }
        printf("\n");
        printf("MQTT: %s\n", data->system.mqtt_connected ? "Connected" : "Disconnected");

        /* Sensor Status */
        printf("\n--- Sensors ---\n");

        /* Derive warming status from sensor states */
        sensor_runtime_info_t info;
        bool any_warming = false;
        for (int i = 0; i < SENSOR_ID_MAX; i++) {
            if (sensor_coordinator_get_runtime_info((sensor_id_t)i, &info) == ESP_OK) {
                if (info.state == SENSOR_STATE_WARMING) {
                    any_warming = true;
                    break;
                }
            }
        }
        printf("Status: %s\n", any_warming ? "Warming up..." : "Ready");
        if (sensor_coordinator_get_runtime_info(SENSOR_ID_SHT41, &info) == ESP_OK) {
            printf("SHT41:   %s\n", (info.state == SENSOR_STATE_READY || info.state == SENSOR_STATE_WARMING) ? "OK" : "FAULT");
        } else {
            printf("SHT41:   FAULT\n");
        }
        if (sensor_coordinator_get_runtime_info(SENSOR_ID_BMP280, &info) == ESP_OK) {
            printf("BMP280:  %s\n", (info.state == SENSOR_STATE_READY || info.state == SENSOR_STATE_WARMING) ? "OK" : "FAULT");
        } else {
            printf("BMP280:  FAULT\n");
        }
        if (sensor_coordinator_get_runtime_info(SENSOR_ID_SGP41, &info) == ESP_OK) {
            printf("SGP41:   %s\n", (info.state == SENSOR_STATE_READY || info.state == SENSOR_STATE_WARMING) ? "OK" : "FAULT");
        } else {
            printf("SGP41:   FAULT\n");
        }
        if (sensor_coordinator_get_runtime_info(SENSOR_ID_PMS5003, &info) == ESP_OK) {
            printf("PMS5003: %s\n", (info.state == SENSOR_STATE_READY || info.state == SENSOR_STATE_WARMING) ? "OK" : "FAULT");
        } else {
            printf("PMS5003: FAULT\n");
        }
        if (sensor_coordinator_get_runtime_info(SENSOR_ID_S8, &info) == ESP_OK) {
            printf("S8:      %s\n", (info.state == SENSOR_STATE_READY || info.state == SENSOR_STATE_WARMING) ? "OK" : "FAULT");
        } else {
            printf("S8:      FAULT\n");
        }

        /* Sensor Readings */
        printf("\n--- Current Readings ---\n");
        if (isnan(data->temperature)) printf("Temperature: n/a\n"); else printf("Temperature: %.1f degC\n", data->temperature);
        if (isnan(data->humidity))    printf("Humidity: n/a\n");    else printf("Humidity: %.1f%%\n", data->humidity);
        if (isnan(data->pressure))    printf("Pressure: n/a\n");    else printf("Pressure: %.1f hPa\n", data->pressure);
        if (isnan(data->mcu_temperature)) printf("MCU Temp: n/a\n"); else printf("MCU Temp: %.1f degC\n", data->mcu_temperature);
        if (isnan(data->co2_ppm))     printf("CO2: n/a\n");       else printf("CO2: %.0f ppm\n", data->co2_ppm);
        if (isnan(data->pm2_5))       printf("PM2.5: n/a\n");     else printf("PM2.5: %.1f ug/m3\n", data->pm2_5);
        if (data->voc_index == UINT16_MAX) printf("VOC Index: n/a\n"); else printf("VOC Index: %u\n", data->voc_index);
        if (data->aqi == UINT16_MAX)       printf("AQI: n/a\n");       else printf("AQI: %u\n", data->aqi);
        printf("Comfort: %s\n", data->comfort ? data->comfort : "unknown");
    }

    printf("\n");
    return 0;
}

/* ==================== RESTART COMMAND ==================== */
static int cmd_restart(int argc, char **argv)
{
    printf("Restarting in 3 seconds...\n");
    vTaskDelay(pdMS_TO_TICKS(3000));
    esp_restart();
    return 0;
}

/* ==================== WIFI COMMANDS ==================== */
static int cmd_wifi_status(int argc, char **argv)
{
    printf("\n=== WiFi Status ===\n");

    char ssid[33];
    wifi_manager_get_ssid(ssid, sizeof(ssid));
    printf("Configured SSID: %s\n", ssid);

    IAQ_DATA_WITH_LOCK() {
        iaq_data_t *data = iaq_data_get();
        printf("Status: %s\n", data->system.wifi_connected ? "Connected" : "Disconnected");
        if (data->system.wifi_connected) {
            printf("RSSI: %ld dBm\n", data->system.wifi_rssi);
        }
    }

    printf("\n");
    return 0;
}

static int cmd_wifi_scan(int argc, char **argv)
{
    printf("\n=== WiFi Scan ===\n");
    printf("Scanning for networks...\n");

    const uint16_t max_aps = 20;
    wifi_ap_record_t *ap_records = (wifi_ap_record_t *)calloc(max_aps, sizeof(wifi_ap_record_t));
    if (!ap_records) {
        printf("Scan failed: out of memory\n");
        return 1;
    }

    uint16_t num_aps = 0;

    esp_err_t ret = wifi_manager_scan(ap_records, max_aps, &num_aps);
    if (ret != ESP_OK) {
        printf("Scan failed: %s\n", esp_err_to_name(ret));
        free(ap_records);
        return 1;
    }

    printf("Found %d networks:\n\n", num_aps);
    printf("%-32s  %-6s  %-4s  %s\n", "SSID", "RSSI", "CH", "AUTH");
    printf("-------------------------------------------------------------------\n");

    for (int i = 0; i < num_aps; i++) {
        const char *auth_str = "OPEN";
        switch (ap_records[i].authmode) {
            case WIFI_AUTH_WEP: auth_str = "WEP"; break;
            case WIFI_AUTH_WPA_PSK: auth_str = "WPA"; break;
            case WIFI_AUTH_WPA2_PSK: auth_str = "WPA2"; break;
            case WIFI_AUTH_WPA_WPA2_PSK: auth_str = "WPA/WPA2"; break;
            case WIFI_AUTH_WPA3_PSK: auth_str = "WPA3"; break;
            case WIFI_AUTH_WPA2_WPA3_PSK: auth_str = "WPA2/WPA3"; break;
            default: break;
        }

        printf("%-32s  %4d  %4d  %s\n",
               ap_records[i].ssid,
               ap_records[i].rssi,
               ap_records[i].primary,
               auth_str);
    }

    printf("\n");
    free(ap_records);
    return 0;
}

static int cmd_wifi_set(int argc, char **argv)
{
    if (argc < 2) {
        printf("Usage: wifi set <ssid> <password>\n");
        return 1;
    }

    const char *ssid = argv[0];
    const char *password = argv[1];

    printf("Setting WiFi credentials...\n");
    printf("SSID: %s\n", ssid);

    esp_err_t ret = wifi_manager_set_credentials(ssid, password);
    if (ret != ESP_OK) {
        printf("Failed to set credentials: %s\n", esp_err_to_name(ret));
        return 1;
    }

    printf("WiFi credentials saved to NVS.\n");
    printf("Restart WiFi with: wifi restart\n");
    printf("\n");
    return 0;
}

static int cmd_wifi_restart(int argc, char **argv)
{
    printf("Restarting WiFi...\n");

    esp_err_t ret = wifi_manager_stop();
    if (ret != ESP_OK) {
        printf("Failed to stop WiFi: %s\n", esp_err_to_name(ret));
        /* Continue with restart attempt for recovery */
    }
    vTaskDelay(pdMS_TO_TICKS(1000));

    ret = wifi_manager_start();
    if (ret != ESP_OK) {
        printf("Failed to start WiFi: %s\n", esp_err_to_name(ret));
        return 1;
    }

    printf("WiFi restarted.\n");
    return 0;
}

static int cmd_wifi(int argc, char **argv)
{
    if (argc < 2) {
        printf("Usage: wifi <status|scan|set|restart>\n");
        printf("  status         - Show WiFi connection status\n");
        printf("  scan           - Scan for available networks\n");
        printf("  set <ssid> <password> - Set WiFi credentials\n");
        printf("  restart        - Restart WiFi connection\n");
        return 0; // printing usage is not an error
    }

    if (strcmp(argv[1], "status") == 0) {
        return cmd_wifi_status(argc - 1, &argv[1]);
    } else if (strcmp(argv[1], "scan") == 0) {
        return cmd_wifi_scan(argc - 1, &argv[1]);
    } else if (strcmp(argv[1], "set") == 0) {
        return cmd_wifi_set(argc - 2, &argv[2]);
    } else if (strcmp(argv[1], "restart") == 0) {
        return cmd_wifi_restart(argc - 1, &argv[1]);
    }

    printf("Unknown wifi command: %s\n", argv[1]);
    return 1;
}

/* ==================== MQTT COMMANDS ==================== */
static int cmd_mqtt_status(int argc, char **argv)
{
    printf("\n=== MQTT Status ===\n");

    char broker_url[128];
    mqtt_manager_get_broker_url(broker_url, sizeof(broker_url));
    if (broker_url[0] == '\0') {
        printf("Broker: (not configured)\n");
        printf("MQTT is disabled until a valid broker is set.\n");
    } else {
        printf("Broker: %s\n", broker_url);
    }

    IAQ_DATA_WITH_LOCK() {
        iaq_data_t *data = iaq_data_get();
        printf("Status: %s\n", data->system.mqtt_connected ? "Connected" : "Disconnected");
    }

    printf("\n");
    return 0;
}

static int cmd_mqtt_publish_test(int argc, char **argv)
{
    printf("Publishing test message...\n");

    /* Snapshot data, release lock, then publish */
    iaq_data_t snapshot;
    IAQ_DATA_WITH_LOCK() {
        snapshot = *iaq_data_get();
    }

    esp_err_t ret = ESP_OK;
    ret |= mqtt_publish_sensor_mcu(&snapshot);
    ret |= mqtt_publish_sensor_sht41(&snapshot);
    ret |= mqtt_publish_sensor_bmp280(&snapshot);
    ret |= mqtt_publish_sensor_sgp41(&snapshot);
    ret |= mqtt_publish_sensor_pms5003(&snapshot);
    ret |= mqtt_publish_sensor_s8(&snapshot);
    if (ret == ESP_OK) printf("Per-sensor test messages published\n");
    else printf("Some per-sensor test publishes may have failed\n");

    return 0;
}

static int cmd_mqtt_set(int argc, char **argv)
{
    if (argc < 1) {
        printf("Usage: mqtt set <broker_url> [username] [password]\n");
        printf("Example: mqtt set mqtt://192.168.1.100:1883\n");
        printf("Example: mqtt set mqtt://broker.local:1883 myuser mypass\n");
        return 1;
    }

    const char *broker_url = argv[0];
    const char *username = (argc >= 2) ? argv[1] : NULL;
    const char *password = (argc >= 3) ? argv[2] : NULL;

    printf("Setting MQTT broker...\n");
    printf("Broker URL: %s\n", broker_url);
    if (username) {
        printf("Username: %s\n", username);
    }

    esp_err_t ret = mqtt_manager_set_broker(broker_url, username, password);
    if (ret != ESP_OK) {
        printf("Failed to set broker: %s\n", esp_err_to_name(ret));
        return 1;
    }

    printf("MQTT broker configuration saved to NVS.\n");
    printf("Restart MQTT with: mqtt restart\n");
    printf("\n");
    return 0;
}

static int cmd_mqtt_restart(int argc, char **argv)
{
    printf("Restarting MQTT...\n");

    mqtt_manager_stop();
    vTaskDelay(pdMS_TO_TICKS(1000));

    esp_err_t ret = mqtt_manager_start();
    if (ret != ESP_OK) {
        printf("Failed to start MQTT: %s\n", esp_err_to_name(ret));
        return 1;
    }

    printf("MQTT restarted.\n");
    return 0;
}

static int cmd_mqtt(int argc, char **argv)
{
    if (argc < 2) {
        printf("Usage: mqtt <status|publish|set|restart>\n");
        printf("  status         - Show MQTT connection status\n");
        printf("  publish        - Publish test message\n");
        printf("  set <url> [user] [pass] - Set MQTT broker\n");
        printf("  restart        - Restart MQTT connection\n");
        return 0;
    }

    if (strcmp(argv[1], "status") == 0) {
        return cmd_mqtt_status(argc - 1, &argv[1]);
    } else if (strcmp(argv[1], "publish") == 0) {
        return cmd_mqtt_publish_test(argc - 1, &argv[1]);
    } else if (strcmp(argv[1], "set") == 0) {
        return cmd_mqtt_set(argc - 2, &argv[2]);
    } else if (strcmp(argv[1], "restart") == 0) {
        return cmd_mqtt_restart(argc - 1, &argv[1]);
    }

    printf("Unknown mqtt command: %s\n", argv[1]);
    return 1;
}

/* ==================== SENSOR COMMANDS ==================== */
static bool parse_sensor_id(const char *name, sensor_id_t *out)
{
    if (!name || !out) return false;
    if (strcmp(name, "mcu") == 0) { *out = SENSOR_ID_MCU; return true; }
    if (strcmp(name, "s8") == 0 || strcmp(name, "co2") == 0) { *out = SENSOR_ID_S8; return true; }
    if (strcmp(name, "sht41") == 0) { *out = SENSOR_ID_SHT41; return true; }
    if (strcmp(name, "bmp280") == 0) { *out = SENSOR_ID_BMP280; return true; }
    if (strcmp(name, "sgp41") == 0) { *out = SENSOR_ID_SGP41; return true; }
    if (strcmp(name, "pms5003") == 0) { *out = SENSOR_ID_PMS5003; return true; }
    return false;
}

static int cmd_sensor_status(int argc, char **argv)
{
    printf("\n=== Sensor Status ===\n\n");

    /* Get runtime info from coordinator for each sensor */
    int64_t now_us = esp_timer_get_time();

    printf("%-10s  %-10s  %-18s  %-8s\n", "Sensor", "State", "Last Update", "Errors");
    printf("-----------------------------------------------------------\n");

    for (int i = 0; i < SENSOR_ID_MAX; i++) {
        sensor_runtime_info_t info;
        if (sensor_coordinator_get_runtime_info((sensor_id_t)i, &info) != ESP_OK) continue;

        const char *name = sensor_coordinator_id_to_name((sensor_id_t)i);
        const char *state = sensor_coordinator_state_to_string(info.state);

        /* Calculate age or show warm-up countdown */
        char age_str[24];
        if (info.state == SENSOR_STATE_WARMING) {
            int64_t remaining_us = info.warmup_deadline_us - now_us;
            if (remaining_us > 0) {
                snprintf(age_str, sizeof(age_str), "%.1fs left", remaining_us / 1e6);
            } else {
                snprintf(age_str, sizeof(age_str), "ready soon");
            }
        } else if (info.last_read_us > 0) {
            int64_t age_s = (now_us - info.last_read_us) / 1000000LL;
            snprintf(age_str, sizeof(age_str), "%llds ago", (long long)age_s);
        } else {
            snprintf(age_str, sizeof(age_str), "never");
        }

        printf("%-10s  %-10s  %-18s  %-8lu\n",
               name, state, age_str, (unsigned long)info.error_count);
    }

    printf("\n");
    return 0;
}

static int cmd_sensor_read(int argc, char **argv)
{
    if (argc < 2) {
        printf("Usage: sensor read <sensor>\n");
        printf("  sensors: mcu, sht41, bmp280, sgp41, pms5003, s8\n");
        return 1;
    }

    const char *sensor = argv[1];
    sensor_id_t id;
    if (!parse_sensor_id(sensor, &id)) {
        printf("Unknown sensor: %s\n", sensor);
        return 1;
    }
    esp_err_t ret = sensor_coordinator_force_read_sync(id, 3000);
    if (ret == ESP_OK) {
        printf("Read '%s': success\n", sensor);
        return 0;
    } else if (ret == ESP_ERR_TIMEOUT) {
        printf("Read '%s': timeout\n", sensor);
        return 1;
    }
    printf("Read '%s': failed: %s\n", sensor, esp_err_to_name(ret));
    return 1;
}

static int cmd_sensor_reset(int argc, char **argv)
{
    if (argc < 2) {
        printf("Usage: sensor reset <sensor>\n");
        printf("  sensors: mcu\n");
        return 1;
    }
    const char *sensor = argv[1];
    sensor_id_t id;
    if (!parse_sensor_id(sensor, &id)) {
        printf("Unknown sensor: %s\n", sensor);
        return 1;
    }
    esp_err_t ret = sensor_coordinator_reset(id);
    if (ret == ESP_OK) {
        printf("Reset request enqueued for '%s'\n", sensor);
        return 0;
    }
    printf("Failed to queue reset request: %s\n", esp_err_to_name(ret));
    return 1;
}

static int cmd_sensor_calibrate(int argc, char **argv)
{
    if (argc < 3) {
        printf("Usage: sensor calibrate co2 <ppm>\n");
        return 1;
    }
    if (strcmp(argv[1], "co2") != 0) {
        printf("Only CO2 calibration is supported here.\n");
        return 1;
    }
    int ppm = atoi(argv[2]);
    if (ppm <= 0) {
        printf("Invalid ppm value: %s\n", argv[2]);
        return 1;
    }
    esp_err_t ret = sensor_coordinator_calibrate(SENSOR_ID_S8, ppm);
    if (ret == ESP_OK) {
        printf("CO2 sensor calibration request enqueued (%d ppm).\n", ppm);
        return 0;
    }
    printf("Failed to queue calibration request: %s\n", esp_err_to_name(ret));
    return 1;
}

static int cmd_sensor_cadence(int argc, char **argv)
{
    if (argc == 1) {
        /* Show cadences */
        uint32_t ms[SENSOR_ID_MAX] = {0};
        bool from_nvs[SENSOR_ID_MAX] = {0};
        esp_err_t ret = sensor_coordinator_get_cadences(ms, from_nvs);
        if (ret != ESP_OK) {
            printf("Failed to get cadences: %s\n", esp_err_to_name(ret));
            return 1;
        }
        printf("\n=== Sensor Cadences ===\n");
        printf("%-10s  %-10s  %-8s\n", "Sensor", "Cadence(ms)", "Source");
        printf("-----------------------------------\n");
        for (int i = 0; i < SENSOR_ID_MAX; ++i) {
            printf("%-10s  %-10lu  %-8s\n",
                   sensor_coordinator_id_to_name((sensor_id_t)i),
                   (unsigned long)ms[i],
                   from_nvs[i] ? "NVS" : "default");
        }
        printf("\n");
        return 0;
    }

    if (argc == 3 && strcmp(argv[1], "set") == 0) {
        sensor_id_t id;
        if (!parse_sensor_id(argv[2], &id)) {
            printf("Unknown sensor: %s\n", argv[2]);
            return 1;
        }
        printf("Usage: sensor cadence set <sensor> <ms>\n");
        return 1;
    }

    if (argc == 4 && strcmp(argv[1], "set") == 0) {
        sensor_id_t id;
        if (!parse_sensor_id(argv[2], &id)) {
            printf("Unknown sensor: %s\n", argv[2]);
            return 1;
        }
        int ms = atoi(argv[3]);
        if (ms < 0) {
            printf("Invalid ms: %s\n", argv[3]);
            return 1;
        }
        esp_err_t ret = sensor_coordinator_set_cadence(id, (uint32_t)ms);
        if (ret == ESP_OK) {
            printf("Cadence for %s set to %d ms (saved to NVS)\n", argv[2], ms);
            return 0;
        }
        printf("Failed to set cadence: %s\n", esp_err_to_name(ret));
        return 1;
    }

    printf("Usage: sensor cadence [set <sensor> <ms>]\n");
    return 1;
}

static int cmd_sensor(int argc, char **argv)
{
    if (argc < 2) {
        printf("Usage: sensor <status|read|reset|calibrate|cadence>\n");
        printf("  status                 - Show sensor health status\n");
        printf("  read <sensor>          - Force read specific sensor (e.g., mcu)\n");
        printf("  reset <sensor>         - Reset specific sensor (e.g., mcu)\n");
        printf("  calibrate co2 <ppm>    - Calibrate CO2 sensor\n");
        printf("  cadence [set <sensor> <ms>] - Show or set cadences\n");
        return 0;
    }

    if (strcmp(argv[1], "status") == 0) {
        return cmd_sensor_status(argc - 1, &argv[1]);
    } else if (strcmp(argv[1], "read") == 0) {
        return cmd_sensor_read(argc - 1, &argv[1]);
    } else if (strcmp(argv[1], "reset") == 0) {
        return cmd_sensor_reset(argc - 1, &argv[1]);
    } else if (strcmp(argv[1], "calibrate") == 0) {
        return cmd_sensor_calibrate(argc - 1, &argv[1]);
    } else if (strcmp(argv[1], "cadence") == 0) {
        return cmd_sensor_cadence(argc - 1, &argv[1]);
    }

    printf("Unknown or unimplemented sensor command: %s\n", argv[1]);
    return 1;
}

/* ==================== FREE COMMAND ==================== */
static int cmd_free(int argc, char **argv)
{
    printf("\n=== Memory Info ===\n");
    printf("Free heap: %lu bytes\n", (unsigned long)esp_get_free_heap_size());
    printf("Min free heap: %lu bytes\n", (unsigned long)esp_get_minimum_free_heap_size());

    /* Show largest free block */
    printf("Largest free block: %lu bytes\n", (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT));

    printf("\n");
    return 0;
}

/* ==================== VERSION COMMAND ==================== */
static int cmd_version(int argc, char **argv)
{
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);

    printf("\n=== System Information ===\n");
    printf("IAQ Monitor v%d.%d.%d\n", IAQ_VERSION_MAJOR, IAQ_VERSION_MINOR, IAQ_VERSION_PATCH);
    printf("IDF Version: %s\n", esp_get_idf_version());
    printf("Chip: ESP32-%s, %d CPU cores\n", CONFIG_IDF_TARGET, chip_info.cores);
    printf("Silicon rev: %d\n", chip_info.revision);

    /* Get flash size */
    uint32_t flash_size = 0;
    if (esp_flash_get_size(NULL, &flash_size) == ESP_OK) {
        printf("Flash: %lu MB %s\n",
               (unsigned long)(flash_size / (1024 * 1024)),
               (chip_info.features & CHIP_FEATURE_EMB_FLASH) ? "embedded" : "external");
    } else {
        printf("Flash: Unknown size\n");
    }
    printf("\n");
    return 0;
}

/* ==================== INITIALIZATION ==================== */
esp_err_t console_commands_init(void)
{
#if CONFIG_IAQ_ENABLE_CONSOLE_COMMANDS
    ESP_LOGI(TAG, "Initializing console commands");

    esp_console_repl_t *repl = NULL;
    esp_console_repl_config_t repl_config = ESP_CONSOLE_REPL_CONFIG_DEFAULT();

    repl_config.prompt = "iaq>";
    repl_config.max_cmdline_length = 256;

    /* Register help command */
    esp_console_register_help_command();

    /* Register status command */
    const esp_console_cmd_t status_cmd = {
        .command = "status",
        .help = "Show comprehensive system status",
        .hint = NULL,
        .func = &cmd_status,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&status_cmd));

    /* Register restart command */
    const esp_console_cmd_t restart_cmd = {
        .command = "restart",
        .help = "Restart the system",
        .hint = NULL,
        .func = &cmd_restart,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&restart_cmd));

    /* Register WiFi commands */
    const esp_console_cmd_t wifi_cmd = {
        .command = "wifi",
        .help = "WiFi management commands",
        .hint = NULL,
        .func = &cmd_wifi,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&wifi_cmd));

    /* Register MQTT commands */
    const esp_console_cmd_t mqtt_cmd = {
        .command = "mqtt",
        .help = "MQTT management commands",
        .hint = NULL,
        .func = &cmd_mqtt,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&mqtt_cmd));

    /* Register sensor commands */
    const esp_console_cmd_t sensor_cmd = {
        .command = "sensor",
        .help = "Sensor control commands",
        .hint = NULL,
        .func = &cmd_sensor,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&sensor_cmd));

    /* Register free command */
    const esp_console_cmd_t free_cmd = {
        .command = "free",
        .help = "Show memory information",
        .hint = NULL,
        .func = &cmd_free,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&free_cmd));

    /* Register version command */
    const esp_console_cmd_t version_cmd = {
        .command = "version",
        .help = "Show version and system information",
        .hint = NULL,
        .func = &cmd_version,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&version_cmd));

    /* Start console REPL */
    esp_console_dev_uart_config_t hw_config = ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_console_new_repl_uart(&hw_config, &repl_config, &repl));
    ESP_ERROR_CHECK(esp_console_start_repl(repl));

    ESP_LOGI(TAG, "Console initialized. Press Enter to activate. Type 'help' for commands.");
#else
    ESP_LOGI(TAG, "Console commands disabled in configuration");
#endif

    return ESP_OK;
}




