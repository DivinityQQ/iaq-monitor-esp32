/* components/console_commands/console_commands.c */
#include <stdio.h>
#include <stdlib.h>
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
#include "esp_wifi.h"
#include "iaq_data.h"
#include "iaq_config.h"
#include "wifi_manager.h"
#include "mqtt_manager.h"
#include "sensor_coordinator.h"
#include "s8_driver.h"
#include "power_board.h"
/* SGP41 baseline ops removed; no direct console hooks needed */

static const char *TAG = "CONSOLE_CMD";

/* Helper: parse one possibly-quoted argument from argv[idx].
 * If argv[idx] begins with a double quote, concatenates subsequent tokens until
 * a token ending with a double quote is found, inserting single spaces between tokens.
 * Surrounding quotes are stripped. Otherwise, copies the single token as-is.
 * On success, writes zero-terminated string to out, advances idx, and returns true. */
static bool parse_one_quoted(int argc, char **argv, int *idx, char *out, size_t out_len)
{
    if (!out || out_len == 0 || !idx || *idx >= argc) {
        return false;
    }

    const char *tok = argv[*idx];
    size_t out_pos = 0;
    int i = *idx;

    if (tok && tok[0] == '"') {
        /* Starts with quote: collect until closing quote */
        const char *p = tok + 1; /* skip leading quote */
        size_t len = strlen(p);
        bool ends_here = false;
        if (len > 0 && p[len - 1] == '"') {
            ends_here = true;
            len -= 1; /* drop trailing quote */
        }
        if (len > 0) {
            size_t copy = len < (out_len - 1 - out_pos) ? len : (out_len - 1 - out_pos);
            memcpy(out + out_pos, p, copy);
            out_pos += copy;
        }
        i++;
        if (!ends_here) {
            for (; i < argc; ++i) {
                const char *frag = argv[i];
                size_t frag_len = strlen(frag);
                bool last = (frag_len > 0 && frag[frag_len - 1] == '"');
                if (out_pos < out_len - 1) out[out_pos++] = ' ';
                if (last) frag_len -= 1; /* drop trailing quote */
                size_t copy = frag_len < (out_len - 1 - out_pos) ? frag_len : (out_len - 1 - out_pos);
                if (copy > 0) {
                    memcpy(out + out_pos, frag, copy);
                    out_pos += copy;
                }
                if (last) { i++; break; }
            }
            if (i > argc) {
                /* Unterminated quotes */
                out[out_pos] = '\0';
                return false;
            }
        }
        *idx = i;
    } else {
        /* Unquoted single token */
        size_t len = strlen(tok);
        size_t copy = len < (out_len - 1) ? len : (out_len - 1);
        memcpy(out, tok, copy);
        out_pos = copy;
        *idx = i + 1;
    }

    out[out_pos] = '\0';
    return true;
}

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
        if (sensor_coordinator_get_runtime_info(SENSOR_ID_SHT45, &info) == ESP_OK) {
            printf("SHT45:   %s\n", (info.state == SENSOR_STATE_READY || info.state == SENSOR_STATE_WARMING) ? "OK" : "FAULT");
        } else {
            printf("SHT45:   FAULT\n");
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

        /* Compensated (Fused) Sensor Readings */
        printf("\n--- Sensor Readings (Compensated) ---\n");
        if (isnan(data->fused.temp_c)) printf("Temperature: n/a\n"); else printf("Temperature: %.1f degC\n", data->fused.temp_c);
        if (isnan(data->fused.rh_pct))    printf("Humidity: n/a\n");    else printf("Humidity: %.1f%%\n", data->fused.rh_pct);
        if (isnan(data->fused.pressure_pa))    printf("Pressure: n/a\n");    else printf("Pressure: %.1f hPa\n", data->fused.pressure_pa / 100.0f);
        if (isnan(data->raw.mcu_temp_c)) printf("MCU Temp: n/a\n"); else printf("MCU Temp: %.1f degC\n", data->raw.mcu_temp_c);
        if (isnan(data->fused.co2_ppm))     printf("CO2: n/a\n");       else printf("CO2: %.0f ppm\n", data->fused.co2_ppm);
        if (isnan(data->fused.pm25_ugm3))       printf("PM2.5: n/a\n");     else printf("PM2.5: %.1f ug/m3\n", data->fused.pm25_ugm3);
        if (data->raw.voc_index == UINT16_MAX) printf("VOC Index: n/a\n"); else printf("VOC Index: %u\n", data->raw.voc_index);

        /* Derived Metrics */
        printf("\n--- Air Quality Metrics ---\n");
        if (data->metrics.aqi_value == UINT16_MAX) printf("AQI: n/a\n"); else printf("AQI: %u (%s)\n", data->metrics.aqi_value, data->metrics.aqi_category);
        printf("Comfort: %s (score: %u/100)\n", data->metrics.comfort_category, data->metrics.comfort_score);
        printf("Overall IAQ Score: %u/100\n", data->metrics.overall_iaq_score);
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

    bool provisioned = wifi_manager_is_provisioned();
    printf("Provisioned: %s\n", provisioned ? "yes" : "no");

    wifi_mode_t mode = wifi_manager_get_mode();
    const char *mode_str = (mode == WIFI_MODE_STA) ? "STA" :
                           (mode == WIFI_MODE_AP) ? "AP" :
                           (mode == WIFI_MODE_APSTA) ? "AP+STA" : "OFF";
    printf("Mode: %s\n", mode_str);

    /* STA details */
    if (mode == WIFI_MODE_STA || mode == WIFI_MODE_APSTA) {
        char ssid[33] = {0};
        wifi_manager_get_ssid(ssid, sizeof(ssid));
        IAQ_DATA_WITH_LOCK() {
            iaq_data_t *data = iaq_data_get();
            printf("STA: SSID=%s, Status=%s", ssid, data->system.wifi_connected ? "Connected" : "Disconnected");
            if (data->system.wifi_connected) {
                printf(" (RSSI: %ld dBm)", data->system.wifi_rssi);
            }
            printf("\n");
        }
    }

    /* AP details */
    if (mode == WIFI_MODE_AP || mode == WIFI_MODE_APSTA) {
        wifi_config_t ap_cfg = {0};
        if (esp_wifi_get_config(WIFI_IF_AP, &ap_cfg) == ESP_OK) {
            const char *auth = "OPEN";
            switch (ap_cfg.ap.authmode) {
                case WIFI_AUTH_WPA2_PSK: auth = "WPA2"; break;
                case WIFI_AUTH_WPA3_PSK: auth = "WPA3"; break;
                case WIFI_AUTH_WPA2_WPA3_PSK: auth = "WPA2/WPA3"; break;
                default: break;
            }
            printf("AP:  SSID=%s, Channel=%d, Auth=%s\n", (char*)ap_cfg.ap.ssid, ap_cfg.ap.channel, auth);
        } else {
            printf("AP:  (config unavailable)\n");
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
        if (ret == ESP_ERR_NOT_SUPPORTED) {
            printf("Note: WiFi scan is not supported while running as SoftAP.\n");
            printf("      Provide credentials (wifi set ... ; wifi restart) to switch to STA,\n");
            printf("      or enable AP+STA in menuconfig to allow scanning while AP is up.\n");
        }
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
    char ssid[33] = {0};
    char password[65] = {0};

    int idx = 0;
    if (!parse_one_quoted(argc, argv, &idx, ssid, sizeof(ssid))) {
        printf("Usage: wifi set <ssid> <password>\n");
        printf("Note: Use quotes for spaces, e.g., \"My SSID\" \"My Password\"\n");
        return 1;
    }
    if (!parse_one_quoted(argc, argv, &idx, password, sizeof(password))) {
        printf("Usage: wifi set <ssid> <password>\n");
        printf("Note: Use quotes for spaces, e.g., \"My SSID\" \"My Password\"\n");
        return 1;
    }

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
        printf("                         Use quotes for spaces, e.g., \"My SSID\" \"My Password\"\n");
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
    printf("Publishing test messages to unified topics...\n");

    /* Snapshot data, release lock, then publish */
    iaq_data_t snapshot;
    IAQ_DATA_WITH_LOCK() {
        snapshot = *iaq_data_get();
    }

    esp_err_t ret = ESP_OK;

    /* Publish all three unified topics */
    ret = mqtt_publish_state(&snapshot);
    if (ret == ESP_OK) printf("  /state published\n");
    else printf("  /state publish failed\n");

    ret = mqtt_publish_metrics(&snapshot);
    if (ret == ESP_OK) printf("  /metrics published\n");
    else printf("  /metrics publish failed\n");

    ret = mqtt_publish_status(&snapshot);
    if (ret == ESP_OK) printf("  /health published\n");
    else printf("  /health publish failed\n");

#ifdef CONFIG_MQTT_PUBLISH_DIAGNOSTICS
    ret = mqtt_publish_diagnostics(&snapshot);
    if (ret == ESP_OK) printf("  /diagnostics published\n");
    else printf("  /diagnostics publish failed\n");
#endif

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

    /* Only start immediately if WiFi is connected. Otherwise, defer until WiFi connects. */
    if (wifi_manager_is_connected()) {
        esp_err_t ret = mqtt_manager_start();
        if (ret != ESP_OK) {
            printf("Failed to start MQTT: %s\n", esp_err_to_name(ret));
            return 1;
        }
        printf("MQTT restarted.\n");
    } else {
        printf("WiFi not connected; MQTT will start automatically after WiFi connects.\n");
    }
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
    if (strcmp(name, "sht45") == 0) { *out = SENSOR_ID_SHT45; return true; }
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
        printf("  sensors: mcu, sht45, bmp280, sgp41, pms5003, s8\n");
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

static int cmd_sensor_disable(int argc, char **argv)
{
    if (argc < 2) {
        printf("Usage: sensor disable <sensor>\n");
        printf("  sensors: mcu, sht45, bmp280, sgp41, pms5003, s8\n");
        return 1;
    }

    const char *sensor = argv[1];
    sensor_id_t id;
    if (!parse_sensor_id(sensor, &id)) {
        printf("Unknown sensor: %s\n", sensor);
        return 1;
    }

    esp_err_t ret = sensor_coordinator_disable(id);
    if (ret == ESP_OK) {
        printf("Sensor '%s' disabled\n", sensor);
        return 0;
    }
    printf("Failed to disable sensor '%s': %s\n", sensor, esp_err_to_name(ret));
    return 1;
}

static int cmd_sensor_enable(int argc, char **argv)
{
    if (argc < 2) {
        printf("Usage: sensor enable <sensor>\n");
        printf("  sensors: mcu, sht45, bmp280, sgp41, pms5003, s8\n");
        return 1;
    }

    const char *sensor = argv[1];
    sensor_id_t id;
    if (!parse_sensor_id(sensor, &id)) {
        printf("Unknown sensor: %s\n", sensor);
        return 1;
    }

    esp_err_t ret = sensor_coordinator_enable(id);
    if (ret == ESP_OK) {
        printf("Sensor '%s' enabled\n", sensor);
        return 0;
    }
    printf("Failed to enable sensor '%s': %s\n", sensor, esp_err_to_name(ret));
    return 1;
}

static int cmd_sensor(int argc, char **argv)
{
    if (argc < 2) {
        printf("Usage: sensor <status|read|reset|calibrate|cadence|disable|enable|s8>\n");
        printf("  status                 - Show sensor health status\n");
        printf("  read <sensor>          - Force read specific sensor (e.g., mcu)\n");
        printf("  reset <sensor>         - Reset specific sensor (e.g., mcu)\n");
        printf("  calibrate co2 <ppm>    - Calibrate CO2 sensor\n");
        printf("  cadence [set <sensor> <ms>] - Show or set cadences\n");
        printf("  disable <sensor>       - Disable sensor (stop reading, hardware sleep if available)\n");
        printf("  enable <sensor>        - Enable sensor (resume reading, wake if needed)\n");
        printf("  s8 status              - Show S8 diagnostics\n");
        printf("  s8 abc <on|off> [hours]- Enable/disable S8 ABC (period in hours)\n");
        /* SGP41 baseline ops removed */
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
    } else if (strcmp(argv[1], "disable") == 0) {
        return cmd_sensor_disable(argc - 1, &argv[1]);
    } else if (strcmp(argv[1], "enable") == 0) {
        return cmd_sensor_enable(argc - 1, &argv[1]);
    } else if (strcmp(argv[1], "s8") == 0) {
        if (argc < 3) {
            printf("Usage: sensor s8 <status|abc> ...\n");
            return 1;
        }
        if (strcmp(argv[2], "status") == 0) {
            s8_diag_t d;
            esp_err_t r = s8_driver_get_diag(&d);
            if (r != ESP_OK) {
                printf("S8 status failed: %s\n", esp_err_to_name(r));
                return 1;
            }
            printf("S8 Diagnostics:\n");
            printf("  Modbus addr: %u\n", d.modbus_addr);
            printf("  Serial:      %u\n", (unsigned)d.serial_number);
            printf("  CO2:         %u ppm\n", (unsigned)d.co2_ppm);
            printf("  MeterStatus: 0x%04X\n", d.meter_status);
            printf("  ABC:         %s (period=%u h)\n", d.abc_enabled ? "enabled" : "disabled", (unsigned)d.abc_period_hours);
            return 0;
        } else if (strcmp(argv[2], "abc") == 0) {
            if (argc < 4) {
                printf("Usage: sensor s8 abc <on|off> [hours]\n");
                return 1;
            }
            bool enable = (strcmp(argv[3], "on") == 0);
            if (!enable && strcmp(argv[3], "off") != 0) {
                printf("Usage: sensor s8 abc <on|off> [hours]\n");
                return 1;
            }
            uint16_t hours = 180;
            if (argc >= 5) {
                int h = atoi(argv[4]);
                if (h < 0 || h > 10000) {
                    printf("Invalid hours: %s\n", argv[4]);
                    return 1;
                }
                hours = (uint16_t)h;
            }
            esp_err_t r = s8_driver_set_abc_enabled(enable, hours);
            if (r == ESP_OK) {
                printf("S8 ABC %s (period=%u h)\n", enable ? "enabled" : "disabled", (unsigned)(enable ? hours : 0));
                return 0;
            }
            printf("Failed to set S8 ABC: %s\n", esp_err_to_name(r));
            return 1;
        } else {
            printf("Unknown S8 subcommand: %s\n", argv[2]);
            return 1;
        }
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

/* ==================== POWER COMMAND (PowerFeather) ==================== */
static bool power_parse_on_off(const char *arg, bool *out)
{
    if (!arg || !out) return false;
    if (strcmp(arg, "on") == 0) { *out = true; return true; }
    if (strcmp(arg, "off") == 0) { *out = false; return true; }
    return false;
}

static bool power_snapshot(iaq_power_snapshot_t *out)
{
    if (!out) return false;
    IAQ_DATA_WITH_LOCK() {
        *out = iaq_data_get()->power;
    }
    if (!out->available) {
        printf("PowerFeather support is not enabled or not initialized.\n");
        return false;
    }
    return true;
}

static void power_print_status(const iaq_power_snapshot_t *snap)
{
    if (!snap) return;
    printf("\n=== PowerFeather ===\n");
    printf("Supply: %s, %u mV, %d mA (maintain %u mV)\n",
           snap->supply_good ? "good" : "not good",
           (unsigned)snap->supply_mv, (int)snap->supply_ma, (unsigned)snap->maintain_mv);
    printf("Rails: EN=%s, 3V3=%s, VSQT=%s, STAT=%s\n",
           snap->en ? "on" : "off",
           snap->v3v_on ? "on" : "off",
           snap->vsqt_on ? "on" : "off",
           snap->stat_on ? "on" : "off");
    printf("Charger: %s, limit=%u mA\n",
           snap->charging_on ? "enabled" : "disabled",
           (unsigned)snap->charge_limit_ma);
    printf("Battery: %u mV, %d mA, %u%% charge, %u%% health, cycles=%u, time_left=%d min, temp=%.1f C\n",
           (unsigned)snap->batt_mv, (int)snap->batt_ma, (unsigned)snap->charge_pct,
           (unsigned)snap->health_pct, (unsigned)snap->cycles, snap->time_left_min, snap->batt_temp_c);
    printf("Alarms: low_v=%u mV, high_v=%u mV, low_pct=%u%%\n",
           (unsigned)snap->alarm_low_v_mv, (unsigned)snap->alarm_high_v_mv, (unsigned)snap->alarm_low_pct);
    printf("\n");
}

static int cmd_power_status(void)
{
    iaq_power_snapshot_t snap = {0};
    if (!power_snapshot(&snap)) return 1;
    power_print_status(&snap);
    return 0;
}

static int cmd_power_rails(int argc, char **argv)
{
    if (!power_board_is_enabled()) {
        printf("PowerFeather support is not enabled or not initialized.\n");
        return 1;
    }
    if (argc < 3) {
        printf("Usage: power rails <en|3v3|vsqt|stat> <on|off>\n");
        return 1;
    }

    const char *rail = argv[1];
    bool on = false;
    if (!power_parse_on_off(argv[2], &on)) {
        printf("Error: state must be on/off\n");
        return 1;
    }

    esp_err_t err = ESP_ERR_INVALID_ARG;
    if (strcmp(rail, "en") == 0) {
        err = power_board_set_en(on);
    } else if (strcmp(rail, "3v3") == 0) {
        err = power_board_enable_3v3(on);
    } else if (strcmp(rail, "vsqt") == 0) {
        err = power_board_enable_vsqt(on);
    } else if (strcmp(rail, "stat") == 0) {
        err = power_board_enable_stat(on);
    } else {
        printf("Error: rail must be en, 3v3, vsqt, or stat\n");
        return 1;
    }

    if (err != ESP_OK) {
        printf("Failed to set %s: %s\n", rail, esp_err_to_name(err));
        return 1;
    }
    printf("Set %s %s\n", rail, on ? "on" : "off");
    return cmd_power_status();
}

static int cmd_power_charger(int argc, char **argv)
{
    if (!power_board_is_enabled()) {
        printf("PowerFeather support is not enabled or not initialized.\n");
        return 1;
    }
    if (argc < 2) {
        printf("Usage: power charger <on|off> [limit_ma]\n");
        return 1;
    }

    bool enable = false;
    if (!power_parse_on_off(argv[1], &enable)) {
        printf("Error: state must be on/off\n");
        return 1;
    }

    bool has_limit = false;
    uint16_t limit_ma = 0;
    if (argc >= 3) {
        int limit = atoi(argv[2]);
        if (limit < 0 || limit > 2000) {
            printf("Error: limit must be 0-2000 mA\n");
            return 1;
        }
        has_limit = true;
        limit_ma = (uint16_t)limit;
    }

    esp_err_t err = power_board_enable_charging(enable);
    if (err == ESP_OK && has_limit) {
        err = power_board_set_charge_limit(limit_ma);
    }
    if (err != ESP_OK) {
        printf("Failed to update charger: %s\n", esp_err_to_name(err));
        return 1;
    }

    printf("Charger %s", enable ? "enabled" : "disabled");
    if (has_limit) printf(" (limit=%u mA)", (unsigned)limit_ma);
    printf("\n");
    return cmd_power_status();
}

static int cmd_power_limit(int argc, char **argv)
{
    if (!power_board_is_enabled()) {
        printf("PowerFeather support is not enabled or not initialized.\n");
        return 1;
    }
    if (argc < 2) {
        printf("Usage: power limit <mA>\n");
        return 1;
    }
    int limit = atoi(argv[1]);
    if (limit < 0 || limit > 2000) {
        printf("Error: limit must be 0-2000 mA\n");
        return 1;
    }
    esp_err_t err = power_board_set_charge_limit((uint16_t)limit);
    if (err != ESP_OK) {
        printf("Failed to set charge limit: %s\n", esp_err_to_name(err));
        return 1;
    }
    printf("Charge limit set to %d mA\n", limit);
    return cmd_power_status();
}

static void power_print_usage(void)
{
    printf("Usage: power <status|rails|charger|limit>\n");
    printf("  status                         Show power/charger snapshot\n");
    printf("  rails <en|3v3|vsqt|stat> <on|off>  Toggle PowerFeather rails\n");
    printf("  charger <on|off> [limit_ma]    Enable/disable charging (optional limit)\n");
    printf("  limit <mA>                     Set charge current limit (0-2000)\n");
}

static int cmd_power(int argc, char **argv)
{
    if (argc < 2) {
        power_print_usage();
        return 0; /* usage only */
    }
    if (strcmp(argv[1], "status") == 0) {
        return cmd_power_status();
    }

    const char *sub = argv[1];
    if (strcmp(sub, "rails") == 0) {
        return cmd_power_rails(argc - 1, &argv[1]);
    } else if (strcmp(sub, "charger") == 0) {
        return cmd_power_charger(argc - 1, &argv[1]);
    } else if (strcmp(sub, "limit") == 0) {
        return cmd_power_limit(argc - 1, &argv[1]);
    }

    printf("Unknown power command: %s\n", sub);
    return 1;
}

/* ==================== DISPLAY COMMAND ==================== */
#if CONFIG_IAQ_OLED_ENABLE
#include "display_oled/display_ui.h"
#include "display_oled/display_driver.h"

static int cmd_display(int argc, char **argv)
{
    if (argc < 2) {
        printf("Usage: display <subcommand>\n");
        printf("Subcommands:\n");
        printf("  status                - Show display status\n");
        printf("  on                    - Wake display indefinitely\n");
        printf("  wake <seconds>        - Wake display for specified seconds (0 = indefinite)\n");
        printf("  off                   - Turn display off\n");
        printf("  next                  - Next screen\n");
        printf("  prev                  - Previous screen\n");
        printf("  screen <0-5>          - Jump to screen by index\n");
        printf("  invert on|off|toggle  - Set or toggle display invert\n");
        printf("  contrast <0-255>      - Set contrast level\n");
        return 1;
    }

    const char *subcmd = argv[1];

    if (strcmp(subcmd, "status") == 0) {
        bool enabled = display_ui_is_enabled();
        printf("Display: %s (screen %d, override=%s)\n",
               enabled ? "on" : "off",
               display_ui_get_screen(),
               display_ui_is_wake_active() ? "yes" : "no");
        return 0;
    }

    if (strcmp(subcmd, "on") == 0) {
        display_ui_wake_for_seconds(0);
        printf("Display woken (indefinite)\n");
        return 0;
    }

    if (strcmp(subcmd, "wake") == 0) {
        if (argc < 3) {
            printf("Error: wake duration required (seconds, 0 = indefinite)\n");
            return 1;
        }
        int seconds = atoi(argv[2]);
        if (seconds < 0) {
            printf("Error: wake duration must be >= 0\n");
            return 1;
        }
        display_ui_wake_for_seconds((uint32_t)seconds);
        if (seconds == 0) {
            printf("Display woken (indefinite)\n");
        } else {
            printf("Display woken for %d second%s\n", seconds, seconds == 1 ? "" : "s");
        }
        return 0;
    }

    if (strcmp(subcmd, "off") == 0) {
        display_ui_set_enabled(false);
        printf("Display turned off\n");
        return 0;
    }

    if (strcmp(subcmd, "next") == 0) {
        display_ui_next_screen();
        printf("Advanced to next screen\n");
        return 0;
    }

    if (strcmp(subcmd, "prev") == 0) {
        display_ui_prev_screen();
        printf("Advanced to previous screen\n");
        return 0;
    }

    if (strcmp(subcmd, "screen") == 0) {
        if (argc < 3) {
            printf("Error: screen index required (0-5)\n");
            return 1;
        }
        int idx = atoi(argv[2]);
        if (idx < 0 || idx > 5) {
            printf("Error: screen index must be 0-5\n");
            return 1;
        }
        esp_err_t err = display_ui_set_screen(idx);
        if (err != ESP_OK) {
            printf("Error: failed to set screen (%s)\n", esp_err_to_name(err));
            return 1;
        }
        printf("Jumped to screen %d\n", idx);
        return 0;
    }

    if (strcmp(subcmd, "invert") == 0) {
        if (argc < 3) {
            printf("Error: specify on, off, or toggle\n");
            return 1;
        }
        const char *mode = argv[2];
        if (strcmp(mode, "on") == 0) {
            esp_err_t err = display_driver_set_invert(true);
            if (err == ESP_OK) {
                printf("Display invert: on\n");
            } else {
                printf("Error setting invert: %s\n", esp_err_to_name(err));
                return 1;
            }
        } else if (strcmp(mode, "off") == 0) {
            esp_err_t err = display_driver_set_invert(false);
            if (err == ESP_OK) {
                printf("Display invert: off\n");
            } else {
                printf("Error setting invert: %s\n", esp_err_to_name(err));
                return 1;
            }
        } else if (strcmp(mode, "toggle") == 0) {
            /* We don't have a getter, so just toggle by setting on then off in rapid succession won't work.
             * For now, just inform user to use on/off explicitly */
            printf("Note: Use 'on' or 'off' explicitly\n");
        } else {
            printf("Error: specify on, off, or toggle\n");
            return 1;
        }
        return 0;
    }

    if (strcmp(subcmd, "contrast") == 0) {
        if (argc < 3) {
            printf("Error: contrast value required (0-255)\n");
            return 1;
        }
        int contrast = atoi(argv[2]);
        if (contrast < 0 || contrast > 255) {
            printf("Error: contrast must be 0-255\n");
            return 1;
        }
        esp_err_t err = display_driver_set_contrast((uint8_t)contrast);
        if (err == ESP_OK) {
            printf("Display contrast set to %d\n", contrast);
        } else {
            printf("Error setting contrast: %s\n", esp_err_to_name(err));
            return 1;
        }
        return 0;
    }

    printf("Error: unknown display subcommand: %s\n", subcmd);
    return 1;
}
#endif /* CONFIG_IAQ_OLED_ENABLE */

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

    /* Register power command (PowerFeather) */
    const esp_console_cmd_t power_cmd = {
        .command = "power",
        .help = "PowerFeather power status/control",
        .hint = NULL,
        .func = &cmd_power,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&power_cmd));

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

#if CONFIG_IAQ_OLED_ENABLE
    /* Register display command */
    const esp_console_cmd_t display_cmd = {
        .command = "display",
        .help = "Display control commands",
        .hint = NULL,
        .func = &cmd_display,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&display_cmd));
#endif

    /* Start console REPL on the selected console backend */
#if CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
    esp_console_dev_usb_serial_jtag_config_t hw_config = ESP_CONSOLE_DEV_USB_SERIAL_JTAG_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_console_new_repl_usb_serial_jtag(&hw_config, &repl_config, &repl));
#elif CONFIG_ESP_CONSOLE_USB_CDC
    esp_console_dev_usb_cdc_config_t hw_config = ESP_CONSOLE_DEV_CDC_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_console_new_repl_usb_cdc(&hw_config, &repl_config, &repl));
#else
    esp_console_dev_uart_config_t hw_config = ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_console_new_repl_uart(&hw_config, &repl_config, &repl));
#endif
    ESP_ERROR_CHECK(esp_console_start_repl(repl));

    ESP_LOGI(TAG, "Console initialized. Press Enter to activate. Type 'help' for commands.");
#else
    ESP_LOGI(TAG, "Console commands disabled in configuration");
#endif

    return ESP_OK;
}
