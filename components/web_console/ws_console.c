/* components/web_console/ws_console.c */
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <arpa/inet.h>
#include "sdkconfig.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_console.h"
#include "esp_timer.h"
#include "esp_http_server.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "iaq_profiler.h"
#include "web_console_internal.h"

static const char *TAG = "WC_CONSOLE";

static int s_console_sock = -1;
static SemaphoreHandle_t s_console_mutex = NULL;
static int64_t s_last_cmd_time = 0;

static esp_err_t send_text(httpd_req_t *req, const char *txt)
{
    httpd_handle_t server = web_console_get_server();
    if (!req || !txt) return ESP_ERR_INVALID_ARG;
    if (!server) return ESP_ERR_INVALID_STATE;
    httpd_ws_frame_t frame = {
        .type = HTTPD_WS_TYPE_TEXT,
        .payload = (uint8_t *)txt,
        .len = strlen(txt),
    };
    esp_err_t err = httpd_ws_send_frame_async(server, httpd_req_to_sockfd(req), &frame);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "WS console send failed: %s", esp_err_to_name(err));
    }
    return err;
}

static void console_client_cleanup(int sock)
{
    if (!s_console_mutex) return;
    if (xSemaphoreTake(s_console_mutex, pdMS_TO_TICKS(WC_MUTEX_TIMEOUT_MS)) != pdTRUE) {
        ESP_LOGE(TAG, "Console mutex timeout in cleanup");
        return;
    }
    if (s_console_sock == sock) {
        s_console_sock = -1;
        s_last_cmd_time = 0;
        ESP_LOGI(TAG, "Console client closed: %d", sock);
    }
    xSemaphoreGive(s_console_mutex);
}

esp_err_t web_console_console_init(void)
{
    if (s_console_mutex) return ESP_OK;
    s_console_mutex = xSemaphoreCreateMutex();
    if (!s_console_mutex) return ESP_ERR_NO_MEM;
    s_console_sock = -1;
    s_last_cmd_time = 0;
    return ESP_OK;
}

void web_console_console_stop(void)
{
    if (s_console_mutex) {
        vSemaphoreDelete(s_console_mutex);
        s_console_mutex = NULL;
    }
    s_console_sock = -1;
    s_last_cmd_time = 0;
}

static esp_err_t ws_console_handler(httpd_req_t *req)
{
    int sock = httpd_req_to_sockfd(req);

    if (req->method == HTTP_GET) {
        if (!web_console_check_auth(req)) {
            httpd_resp_set_status(req, "401 Unauthorized");
            (void)httpd_resp_send(req, NULL, 0);
            return ESP_FAIL;
        }

        if (!s_console_mutex) {
            ESP_LOGE(TAG, "Console mutex not initialized");
            return ESP_FAIL;
        }
        if (xSemaphoreTake(s_console_mutex, pdMS_TO_TICKS(WC_MUTEX_TIMEOUT_MS)) != pdTRUE) {
            ESP_LOGE(TAG, "Console mutex timeout");
            return ESP_FAIL;
        }
        if (s_console_sock >= 0) {
            /* Gracefully reject with proper close code + reason */
            xSemaphoreGive(s_console_mutex);
            static const char *reason = "Console busy";
            uint16_t code = htons(1013); /* Try Again Later */
            uint8_t payload[2 + sizeof("Console busy")]; /* includes NUL but len excludes */
            memcpy(payload, &code, sizeof(code));
            memcpy(payload + 2, reason, strlen(reason));
            httpd_ws_frame_t closefrm = {
                .type = HTTPD_WS_TYPE_CLOSE,
                .payload = payload,
                .len = 2 + strlen(reason),
            };
            (void)httpd_ws_send_frame(req, &closefrm);
            return ESP_OK;
        }
        s_console_sock = sock;
        s_last_cmd_time = 0;
        xSemaphoreGive(s_console_mutex);

        ESP_LOGI(TAG, "Console client connected: %d", sock);
        return send_text(req, "Connected to IAQ Console\r\niaq> ");
    }

    httpd_ws_frame_t frame = {0};
    esp_err_t ret = httpd_ws_recv_frame(req, &frame, 0);
    if (ret != ESP_OK) {
        console_client_cleanup(sock);
        return ret;
    }

    /* Handle zero-length control frames without blocking on payload */
    if (frame.len == 0) {
        if (frame.type == HTTPD_WS_TYPE_CLOSE) {
            console_client_cleanup(sock);
            httpd_ws_frame_t closefrm = { .type = HTTPD_WS_TYPE_CLOSE };
            (void)httpd_ws_send_frame(req, &closefrm);
            return ESP_OK;
        }
        if (frame.type == HTTPD_WS_TYPE_PING) {
            httpd_ws_frame_t pong = { .type = HTTPD_WS_TYPE_PONG, .payload = NULL, .len = 0 };
            (void)httpd_ws_send_frame(req, &pong);
            return ESP_OK;
        }
        if (frame.type != HTTPD_WS_TYPE_TEXT) {
            return ESP_OK;
        }
        /* Empty text frame -> just re-prompt */
        send_text(req, "iaq> ");
        return ESP_OK;
    }

    /* Bound allocation BEFORE malloc to prevent malicious large frames */
    size_t max_len = CONFIG_IAQ_WEB_CONSOLE_MAX_CMD_LEN;
    if (frame.type == HTTPD_WS_TYPE_TEXT && frame.len > max_len) {
        /* Reject oversized text frames without allocating */
        send_text(req, "[error: command too long]\r\niaq> ");
        return ESP_OK;
    }

    /* For control frames (CLOSE/PING), cap at 125 bytes per RFC 6455 */
    size_t alloc_len = frame.len;
    if (frame.type == HTTPD_WS_TYPE_CLOSE || frame.type == HTTPD_WS_TYPE_PING) {
        if (alloc_len > 125) alloc_len = 125;
    }

    frame.payload = malloc(alloc_len + 1);
    if (!frame.payload) {
        console_client_cleanup(sock);
        return ESP_ERR_NO_MEM;
    }
    ret = httpd_ws_recv_frame(req, &frame, alloc_len);
    if (ret != ESP_OK) {
        free(frame.payload);
        console_client_cleanup(sock);
        return ret;
    }
    frame.payload[frame.len] = '\0';

    if (frame.type == HTTPD_WS_TYPE_CLOSE) {
        console_client_cleanup(sock);
        httpd_ws_frame_t closefrm = { .type = HTTPD_WS_TYPE_CLOSE };
        (void)httpd_ws_send_frame(req, &closefrm);
        free(frame.payload);
        return ESP_OK;
    }
    if (frame.type == HTTPD_WS_TYPE_PING) {
        frame.type = HTTPD_WS_TYPE_PONG;
        (void)httpd_ws_send_frame(req, &frame);
        free(frame.payload);
        return ESP_OK;
    }
    if (frame.type != HTTPD_WS_TYPE_TEXT) {
        free(frame.payload);
        return ESP_OK;
    }

    int64_t now = esp_timer_get_time();
    if (s_last_cmd_time > 0) {
        int64_t min_interval = 1000000LL / CONFIG_IAQ_WEB_CONSOLE_CMD_RATE_LIMIT;
        if ((now - s_last_cmd_time) < min_interval) {
            send_text(req, "[rate limited]\r\niaq> ");
            free(frame.payload);
            return ESP_OK;
        }
    }
    s_last_cmd_time = now;

    /* Strip trailing whitespace/newlines (websocat sends \n or \r\n) */
    char *cmd = (char *)frame.payload;
    size_t len = frame.len;
    while (len > 0 && (cmd[len - 1] == '\n' || cmd[len - 1] == '\r' || cmd[len - 1] == ' ')) {
        cmd[--len] = '\0';
    }

    /* Skip empty commands */
    if (len == 0) {
        send_text(req, "iaq> ");
        free(frame.payload);
        return ESP_OK;
    }

    uint64_t t0 = iaq_prof_tic();
    int ret_code = 0;
    esp_console_run(cmd, &ret_code);

    char prompt[32];
    snprintf(prompt, sizeof(prompt), "(%d) iaq> ", ret_code);
    send_text(req, prompt);
    iaq_prof_toc(IAQ_METRIC_WEB_CONSOLE_CMD, t0);

    free(frame.payload);
    return ESP_OK;
}

void web_console_reset_console_state(void)
{
    if (s_console_mutex) {
        bool locked = (xSemaphoreTake(s_console_mutex, pdMS_TO_TICKS(WC_MUTEX_TIMEOUT_MS)) == pdTRUE);
        if (!locked) {
            ESP_LOGE(TAG, "Console mutex timeout in reset");
        }
        s_console_sock = -1;
        s_last_cmd_time = 0;
        if (locked) xSemaphoreGive(s_console_mutex);
    } else {
        s_console_sock = -1;
        s_last_cmd_time = 0;
    }
}

const httpd_uri_t web_console_uri_console = {
    .uri = "/ws/console",
    .method = HTTP_GET,
    .handler = ws_console_handler,
    .is_websocket = true,
    .handle_ws_control_frames = true,
};
