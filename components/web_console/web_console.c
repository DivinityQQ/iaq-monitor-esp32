/* components/web_console/web_console.c */
#include <string.h>
#include <strings.h>
#include "sdkconfig.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_http_server.h"
#include "web_console.h"
#include "web_console_internal.h"

static const char *TAG = "WEB_CONSOLE";

static httpd_handle_t s_server = NULL;
static bool s_initialized = false;

httpd_handle_t web_console_get_server(void)
{
    return s_server;
}

void web_console_set_server(httpd_handle_t server)
{
    s_server = server;
}

/* Parse token from query parameter ?token=XXX */
bool web_console_check_auth(httpd_req_t *req)
{
    if (!req) return false;

    /* Empty token in Kconfig = reject all connections. */
    if (strlen(CONFIG_IAQ_WEB_CONSOLE_TOKEN) == 0) return false;

    size_t query_len = httpd_req_get_url_query_len(req);
    if (query_len == 0 || query_len >= 256) return false;

    char query[256];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK) {
        return false;
    }

    char token[128];
    if (httpd_query_key_value(query, "token", token, sizeof(token)) != ESP_OK) {
        return false;
    }

    return strcmp(token, CONFIG_IAQ_WEB_CONSOLE_TOKEN) == 0;
}

esp_err_t web_console_init(void)
{
#if !CONFIG_IAQ_WEB_CONSOLE_ENABLE
    return ESP_OK;
#endif

    if (s_initialized) return ESP_OK;

    if (strlen(CONFIG_IAQ_WEB_CONSOLE_TOKEN) == 0) {
        ESP_LOGW(TAG, "IAQ_WEB_CONSOLE_TOKEN is empty; all web console connections will be rejected");
    }

    esp_err_t err = web_console_log_init();
    if (err != ESP_OK) {
        return err;
    }
    err = web_console_console_init();
    if (err != ESP_OK) {
        web_console_log_stop();
        return err;
    }

    s_initialized = true;
    return ESP_OK;
}

bool web_console_is_initialized(void)
{
    return s_initialized;
}

void web_console_reset_clients(void)
{
    web_console_reset_console_state();
    web_console_reset_log_state();
}

void web_console_stop(void)
{
    if (!s_initialized) return;

    s_server = NULL;
    web_console_log_stop();
    web_console_console_stop();
    s_initialized = false;
    ESP_LOGI(TAG, "Web console stopped");
}
