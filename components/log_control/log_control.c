/* components/log_control/log_control.c */
#include "log_control.h"
#include "nvs.h"
#include "esp_log.h"
#include "sdkconfig.h"

#define LOG_CTRL_NVS_NS  "log_cfg"
#define LOG_CTRL_KEY_APP "app_level"
#define LOG_CTRL_KEY_SYS "sys_level"

#define LOG_CTRL_DEFAULT_APP_LEVEL ((esp_log_level_t)CONFIG_LOG_DEFAULT_LEVEL)
#define LOG_CTRL_DEFAULT_SYS_LEVEL ((esp_log_level_t)CONFIG_LOG_DEFAULT_LEVEL)

static const char *TAG = "LOG_CTRL";

static const char *s_system_tags[] = {
    "wifi",
    "wifi_init",
    "net80211",
    "pp",
    "phy_init",
    "esp_netif",
    "esp_event",
    "lwip",
    "dhcpc",
    "dhcps",
    "wpa",
    "esp_https_server",
    "httpd",
    "httpd_sess",
    "httpd_txrx",
    "mdns",
};

static esp_log_level_t s_app_level = LOG_CTRL_DEFAULT_APP_LEVEL;
static esp_log_level_t s_sys_level = LOG_CTRL_DEFAULT_SYS_LEVEL;

static bool log_level_valid(esp_log_level_t level)
{
    return (level >= ESP_LOG_NONE) && (level <= ESP_LOG_VERBOSE);
}

static bool log_level_allowed(esp_log_level_t level)
{
    if (!log_level_valid(level)) return false;
    if (level > CONFIG_LOG_MAXIMUM_LEVEL) return false;
    return true;
}

static void apply_levels(esp_log_level_t app_level, esp_log_level_t sys_level)
{
    esp_log_level_set("*", app_level);
    for (size_t i = 0; i < (sizeof(s_system_tags) / sizeof(s_system_tags[0])); ++i) {
        esp_log_level_set(s_system_tags[i], sys_level);
    }
    s_app_level = app_level;
    s_sys_level = sys_level;
}

static esp_err_t load_level(const char *key, esp_log_level_t default_level, esp_log_level_t *out_level)
{
    if (!key || !out_level) return ESP_ERR_INVALID_ARG;

    nvs_handle_t h;
    esp_err_t err = nvs_open(LOG_CTRL_NVS_NS, NVS_READONLY, &h);
    if (err != ESP_OK) {
        *out_level = default_level;
        return err;
    }

    uint8_t val = (uint8_t)default_level;
    err = nvs_get_u8(h, key, &val);
    nvs_close(h);

    if (err == ESP_OK) {
        esp_log_level_t level = (esp_log_level_t)val;
        if (log_level_allowed(level)) {
            *out_level = level;
            return ESP_OK;
        }
        *out_level = default_level;
        return ESP_ERR_INVALID_ARG;
    }

    *out_level = default_level;
    return err;
}

static esp_err_t save_level(const char *key, esp_log_level_t level)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(LOG_CTRL_NVS_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;

    err = nvs_set_u8(h, key, (uint8_t)level);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err;
}

static esp_err_t erase_level(const char *key)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(LOG_CTRL_NVS_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;

    err = nvs_erase_key(h, key);
    if (err == ESP_ERR_NVS_NOT_FOUND) err = ESP_OK;
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err;
}

esp_err_t log_control_apply_from_nvs(void)
{
#if !CONFIG_LOG_DYNAMIC_LEVEL_CONTROL
    return ESP_ERR_NOT_SUPPORTED;
#else
    esp_log_level_t app_level = LOG_CTRL_DEFAULT_APP_LEVEL;
    esp_log_level_t sys_level = LOG_CTRL_DEFAULT_SYS_LEVEL;
    esp_err_t err_app = load_level(LOG_CTRL_KEY_APP, LOG_CTRL_DEFAULT_APP_LEVEL, &app_level);
    esp_err_t err_sys = load_level(LOG_CTRL_KEY_SYS, LOG_CTRL_DEFAULT_SYS_LEVEL, &sys_level);
    apply_levels(app_level, sys_level);

    if (err_app != ESP_OK && err_app != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "Failed to load app log level: %s", esp_err_to_name(err_app));
        return err_app;
    }
    if (err_sys != ESP_OK && err_sys != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "Failed to load system log level: %s", esp_err_to_name(err_sys));
        return err_sys;
    }
    return ESP_OK;
#endif
}

esp_err_t log_control_set_app_level(esp_log_level_t level, bool persist)
{
#if !CONFIG_LOG_DYNAMIC_LEVEL_CONTROL
    (void)level;
    (void)persist;
    return ESP_ERR_NOT_SUPPORTED;
#else
    if (!log_level_allowed(level)) return ESP_ERR_INVALID_ARG;
    apply_levels(level, s_sys_level);
    if (!persist) return ESP_OK;
    return save_level(LOG_CTRL_KEY_APP, level);
#endif
}

esp_err_t log_control_set_system_level(esp_log_level_t level, bool persist)
{
#if !CONFIG_LOG_DYNAMIC_LEVEL_CONTROL
    (void)level;
    (void)persist;
    return ESP_ERR_NOT_SUPPORTED;
#else
    if (!log_level_allowed(level)) return ESP_ERR_INVALID_ARG;
    apply_levels(s_app_level, level);
    if (!persist) return ESP_OK;
    return save_level(LOG_CTRL_KEY_SYS, level);
#endif
}

esp_err_t log_control_reset_to_defaults(bool persist)
{
#if !CONFIG_LOG_DYNAMIC_LEVEL_CONTROL
    (void)persist;
    return ESP_ERR_NOT_SUPPORTED;
#else
    apply_levels(LOG_CTRL_DEFAULT_APP_LEVEL, LOG_CTRL_DEFAULT_SYS_LEVEL);
    if (!persist) return ESP_OK;

    /* Attempt to erase both keys regardless of individual failures */
    esp_err_t err_app = erase_level(LOG_CTRL_KEY_APP);
    esp_err_t err_sys = erase_level(LOG_CTRL_KEY_SYS);

    /* Return first error encountered, if any */
    return (err_app != ESP_OK) ? err_app : err_sys;
#endif
}

esp_log_level_t log_control_get_app_level(void)
{
    return s_app_level;
}

esp_log_level_t log_control_get_system_level(void)
{
    return s_sys_level;
}

void log_control_get_system_tags(const char ***tags, size_t *count)
{
    if (tags) *tags = s_system_tags;
    if (count) *count = sizeof(s_system_tags) / sizeof(s_system_tags[0]);
}
