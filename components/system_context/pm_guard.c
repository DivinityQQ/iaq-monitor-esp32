/* components/system_context/pm_guard.c */
#include "pm_guard.h"
#include "sdkconfig.h"
#include "esp_pm.h"
#include "esp_log.h"

static const char *TAG = "PM_GUARD";

static bool s_initialized = false;
static bool s_enabled = false;
static esp_pm_lock_handle_t s_lock_cpu = NULL;
static esp_pm_lock_handle_t s_lock_apb = NULL;
static esp_pm_lock_handle_t s_lock_no_sleep = NULL;

esp_err_t pm_guard_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }

#if CONFIG_PM_ENABLE
    /* Configure runtime PM: allow DFS + light sleep, keep max at default CPU freq. */
    const int max_freq_mhz = CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ;
    /* ESP32-S3 supports 80/160/240 MHz; use 80 as the low end for DFS. */
    const int min_freq_mhz = 80;
    esp_pm_config_t pm_cfg = {
        .max_freq_mhz = max_freq_mhz,
        .min_freq_mhz = min_freq_mhz,
        .light_sleep_enable = true,
    };

    esp_err_t err = esp_pm_configure(&pm_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_pm_configure failed: %s", esp_err_to_name(err));
        return err;
    }

    err = esp_pm_lock_create(ESP_PM_CPU_FREQ_MAX, 0, "cpu", &s_lock_cpu);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create CPU PM lock: %s", esp_err_to_name(err));
        return err;
    }

    err = esp_pm_lock_create(ESP_PM_APB_FREQ_MAX, 0, "bus", &s_lock_apb);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create APB PM lock: %s", esp_err_to_name(err));
        (void)esp_pm_lock_delete(s_lock_cpu);
        s_lock_cpu = NULL;
        return err;
    }

    err = esp_pm_lock_create(ESP_PM_NO_LIGHT_SLEEP, 0, "no_ls", &s_lock_no_sleep);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create no-light-sleep PM lock: %s", esp_err_to_name(err));
        /* Clean up APB lock to allow a clean retry on next init call */
        (void)esp_pm_lock_delete(s_lock_apb);
        s_lock_apb = NULL;
        (void)esp_pm_lock_delete(s_lock_cpu);
        s_lock_cpu = NULL;
        return err;
    }

    s_enabled = true;
    ESP_LOGI(TAG, "PM configured: min=%d MHz max=%d MHz, light sleep enabled", min_freq_mhz, max_freq_mhz);
#else
    ESP_LOGI(TAG, "CONFIG_PM_ENABLE is disabled; pm_guard is a no-op");
#endif

    s_initialized = true;
    return ESP_OK;
}

bool pm_guard_is_enabled(void)
{
    return s_enabled;
}

void pm_guard_lock_bus(void)
{
    if (s_lock_apb) {
        (void)esp_pm_lock_acquire(s_lock_apb);
    }
}

void pm_guard_unlock_bus(void)
{
    if (s_lock_apb) {
        (void)esp_pm_lock_release(s_lock_apb);
    }
}

void pm_guard_lock_cpu(void)
{
    if (s_lock_cpu) {
        (void)esp_pm_lock_acquire(s_lock_cpu);
    }
}

void pm_guard_unlock_cpu(void)
{
    if (s_lock_cpu) {
        (void)esp_pm_lock_release(s_lock_cpu);
    }
}

void pm_guard_lock_no_sleep(void)
{
    if (s_lock_no_sleep) {
        (void)esp_pm_lock_acquire(s_lock_no_sleep);
    }
}

void pm_guard_unlock_no_sleep(void)
{
    if (s_lock_no_sleep) {
        (void)esp_pm_lock_release(s_lock_no_sleep);
    }
}
