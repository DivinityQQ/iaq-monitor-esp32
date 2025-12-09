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

/* Parse Bearer token from Authorization or Sec-WebSocket-Protocol. */
web_console_auth_result_t web_console_check_auth(httpd_req_t *req, char *token_buf, size_t buf_len)
{
    web_console_auth_result_t result = { .valid = false, .via_subproto = false, .subproto_echo = {0} };

    if (!req || !token_buf || buf_len == 0) {
        return result;
    }

    /* Empty token in Kconfig = no valid token can be provided = reject all connections.
     * To allow access, configure a non-empty IAQ_WEB_CONSOLE_TOKEN. */
    if (strlen(CONFIG_IAQ_WEB_CONSOLE_TOKEN) == 0) {
        return result;
    }

    /* Method 1: Authorization: Bearer <token> */
    char auth_hdr[256];
    esp_err_t hdr_err = httpd_req_get_hdr_value_str(req, "Authorization", auth_hdr, sizeof(auth_hdr));
    if (hdr_err == ESP_ERR_HTTPD_RESULT_TRUNC) {
        ESP_LOGW(TAG, "Authorization header truncated; token likely too long");
    }
    if (hdr_err == ESP_OK) {
        if (strncasecmp(auth_hdr, "bearer ", 7) == 0) {
            const char *tok = auth_hdr + 7;
            while (*tok == ' ') tok++;
            size_t len = strlen(tok);
            while (len > 0 && tok[len - 1] == ' ') len--;
            if (len > 0 && len < buf_len) {
                memcpy(token_buf, tok, len);
                token_buf[len] = '\0';
                if (strcmp(token_buf, CONFIG_IAQ_WEB_CONSOLE_TOKEN) == 0) {
                    result.valid = true;
                    return result;
                }
            }
        }
    }

    /* Method 2: Sec-WebSocket-Protocol: bearer,<token> (comma separated list) */
    char proto_hdr[256];  /* Reduced from 512; ample for bearer + token */
    hdr_err = httpd_req_get_hdr_value_str(req, "Sec-WebSocket-Protocol", proto_hdr, sizeof(proto_hdr));
    if (hdr_err == ESP_ERR_HTTPD_RESULT_TRUNC) {
        ESP_LOGW(TAG, "Sec-WebSocket-Protocol header truncated; token likely too long");
    }
    if (hdr_err == ESP_OK) {
        char *saveptr = NULL;
        char *entry = strtok_r(proto_hdr, ",", &saveptr);
        while (entry) {
            while (*entry == ' ') entry++;
            char *end = entry + strlen(entry) - 1;
            while (end > entry && *end == ' ') *end-- = '\0';

            if (strncasecmp(entry, "bearer", 6) == 0) {
                const char *tok = entry + 6;
                while (*tok == ' ') tok++;
                /* If no token follows "bearer", keep scanning other entries */
                if (*tok == '\0') {
                    strlcpy(result.subproto_echo, "bearer", sizeof(result.subproto_echo));
                } else if (strlen(tok) < buf_len) {
                    strlcpy(token_buf, tok, buf_len);
                    if (strcmp(token_buf, CONFIG_IAQ_WEB_CONSOLE_TOKEN) == 0) {
                        result.valid = true;
                        result.via_subproto = true;
                        strlcpy(result.subproto_echo, "bearer", sizeof(result.subproto_echo));
                        return result;
                    }
                }
            }
            entry = strtok_r(NULL, ",", &saveptr);
        }
    }

    return result;
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
