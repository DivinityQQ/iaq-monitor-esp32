/* components/time_sync/time_sync.c */
#include <string.h>
#include <time.h>
#include <sys/time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "esp_event.h"
#include "esp_sntp.h"
#include "sdkconfig.h"

#include "iaq_config.h"
#include "system_context.h"
#include "time_sync.h"

static const char *TAG = "TIME_SYNC";

static iaq_system_context_t *s_ctx = NULL;
static volatile bool s_time_set = false;
static volatile bool s_sntp_started = false;

/* Kconfig defaults (can be adjusted later if exposed) */
#ifndef CONFIG_IAQ_TZ_STRING
#define CONFIG_IAQ_TZ_STRING "CET-1CEST,M3.5.0/2,M10.5.0/3"
#endif
#ifndef CONFIG_IAQ_NTP_SERVER0
#define CONFIG_IAQ_NTP_SERVER0 "pool.ntp.org"
#endif
#ifndef CONFIG_IAQ_NTP_SERVER1
#define CONFIG_IAQ_NTP_SERVER1 "time.google.com"
#endif

/* Handle SNTP sync notifications (called from SNTP task context) */
static void time_sync_notification_cb(struct timeval *tv)
{
    (void)tv;
    s_time_set = true;
    if (s_ctx && s_ctx->event_group) {
        xEventGroupSetBits(s_ctx->event_group, TIME_SYNCED_BIT);
    }
    /* Post app-level event for interested components */
    esp_event_post(IAQ_EVENT, IAQ_EVENT_TIME_SYNCED, NULL, 0, 0);

    /* Log sync status (compat with IDF 4.x/5.x enum names) */
#ifndef ESP_SNTP_SYNC_STATUS_IN_PROGRESS
#define ESP_SNTP_SYNC_STATUS_IN_PROGRESS SNTP_SYNC_STATUS_IN_PROGRESS
#endif
    if (esp_sntp_get_sync_status() == ESP_SNTP_SYNC_STATUS_IN_PROGRESS) {
        ESP_LOGI(TAG, "SNTP sync in progress (slewing)");
    }

    time_t now = 0; time(&now);
    struct tm t; localtime_r(&now, &t);
    char buf[32];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &t);
    ESP_LOGI(TAG, "Time synchronized: %s", buf);
}

static void setup_timezone(void)
{
    setenv("TZ", CONFIG_IAQ_TZ_STRING, 1);
    tzset();
}

static void init_sntp_if_needed(void)
{
    if (s_sntp_started) return;

    ESP_LOGI(TAG, "Initializing SNTP");
    esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
    /* Use immediate sync to avoid long waits/inconsistency on restart. */
#ifndef ESP_SNTP_SYNC_MODE_IMMED
#define ESP_SNTP_SYNC_MODE_IMMED SNTP_SYNC_MODE_IMMED
#endif
    esp_sntp_set_sync_mode(ESP_SNTP_SYNC_MODE_IMMED);
    esp_sntp_set_time_sync_notification_cb(time_sync_notification_cb);

    /* Configure servers */
    esp_sntp_setservername(0, CONFIG_IAQ_NTP_SERVER0);
#if LWIP_DHCP_GET_NTP_SRV
    esp_sntp_servermode_dhcp(true);
#endif
    /* Optional second server */
    /* Respect SDK config for max servers to avoid out-of-range index */
#if defined(CONFIG_LWIP_SNTP_MAX_SERVERS) && (CONFIG_LWIP_SNTP_MAX_SERVERS > 1)
    if (strlen(CONFIG_IAQ_NTP_SERVER1) > 0) {
        esp_sntp_setservername(1, CONFIG_IAQ_NTP_SERVER1);
    }
#endif

    esp_sntp_init();
    s_sntp_started = true;
}

static void iaq_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg; (void)data;
    if (base != IAQ_EVENT) return;
    if (id == IAQ_EVENT_WIFI_CONNECTED) {
        /* Start SNTP on first WiFi connect */
        init_sntp_if_needed();
    }
}

esp_err_t time_sync_init(iaq_system_context_t *ctx)
{
    if (!ctx) return ESP_ERR_INVALID_ARG;
    s_ctx = ctx;

    setup_timezone();
    /* Register for IAQ events to start SNTP when WiFi is up */
    esp_err_t err = esp_event_handler_register(IAQ_EVENT, ESP_EVENT_ANY_ID, &iaq_event_handler, NULL);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }

    /* If clock already looks sane (e.g., preserved across reboot), mark as synced. */
    time_t now = 0; time(&now);
    if (now >= 1577836800L) {
        s_time_set = true;
        if (s_ctx && s_ctx->event_group) {
            xEventGroupSetBits(s_ctx->event_group, TIME_SYNCED_BIT);
        }
        esp_event_post(IAQ_EVENT, IAQ_EVENT_TIME_SYNCED, NULL, 0, 0);
        ESP_LOGI(TAG, "Initial clock is sane; marking time as synced");
    }
    ESP_LOGI(TAG, "Time sync initialized (TZ=%s)", CONFIG_IAQ_TZ_STRING);
    return ESP_OK;
}

esp_err_t time_sync_start(void)
{
    init_sntp_if_needed();
    return ESP_OK;
}

bool time_sync_is_set(void)
{
    /* Prefer explicit sync signal if available for consistency */
    if (s_ctx && s_ctx->event_group) {
        EventBits_t bits = xEventGroupGetBits(s_ctx->event_group);
        if ((bits & TIME_SYNCED_BIT) != 0) return true;
    }
    /* Fallback: consider time valid if beyond Jan 1, 2020 (1577836800) */
    time_t now = 0; time(&now);
    return now >= 1577836800L;
}

esp_err_t time_sync_wait_for(int timeout_ms)
{
    const TickType_t delay = pdMS_TO_TICKS(200);
    TickType_t waited = 0;
    TickType_t limit = (timeout_ms < 0) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);

    if (!s_sntp_started) init_sntp_if_needed();

    while (!time_sync_is_set()) {
        vTaskDelay(delay);
        if (limit != portMAX_DELAY) {
            waited += delay;
            if (waited >= limit) return ESP_ERR_TIMEOUT;
        }
    }
    return ESP_OK;
}
