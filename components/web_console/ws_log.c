/* components/web_console/ws_log.c */
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <sys/reent.h>
#include "sdkconfig.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_http_server.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "iaq_profiler.h"
#include "iaq_config.h"
#include "web_console_internal.h"

static const char *TAG = "WC_LOG";
#define LOG_SEND_BATCH_SIZE   CONFIG_IAQ_WEB_CONSOLE_LOG_BUFFER_SIZE

typedef struct {
    char *buffer;
    size_t size;
    size_t head;            /* next write */
    size_t tail;            /* oldest byte */
    size_t broadcast_tail;  /* next byte to broadcast */
} log_ring_t;

typedef struct {
    int sock;
    bool active;
    uint32_t send_failures;
} log_client_t;

/* Static ring buffer - available from first instruction for early boot capture. */
static char s_ring_buffer[CONFIG_IAQ_WEB_CONSOLE_LOG_BUFFER_SIZE];
static log_ring_t s_ring = {
    .buffer = s_ring_buffer,
    .size = CONFIG_IAQ_WEB_CONSOLE_LOG_BUFFER_SIZE,
};
static SemaphoreHandle_t s_ring_mutex = NULL;
static SemaphoreHandle_t s_clients_mutex = NULL;
static QueueHandle_t s_notify_queue = NULL;
static TaskHandle_t s_broadcast_task = NULL;
static bool s_exit_task = false;
static log_client_t s_clients[CONFIG_IAQ_WEB_CONSOLE_MAX_LOG_CLIENTS] = {0};

static inline size_t ring_used(const log_ring_t *rb)
{
    if (!rb || rb->size == 0) return 0;
    if (rb->head >= rb->tail) return rb->head - rb->tail;
    return rb->size - (rb->tail - rb->head);
}

/* Check if cursor is in valid range between tail and head (handles wraparound). */
static inline bool cursor_in_range(size_t cursor, size_t tail, size_t head, size_t size)
{
    if (size == 0) return false;
    if (head >= tail) {
        /* No wrap: valid range is [tail, head] */
        return cursor >= tail && cursor <= head;
    } else {
        /* Wrapped: valid if cursor >= tail OR cursor <= head */
        return cursor >= tail || cursor <= head;
    }
}

/* Read a line from ring buffer using memcpy for contiguous spans.
 * Returns number of bytes read and updates *cursor_out to new position.
 * Stops at newline (included) or when out buffer is full. */
static size_t ring_read_line(const log_ring_t *rb, size_t cursor, size_t head,
                              char *out, size_t out_size, size_t *cursor_out)
{
    size_t len = 0;
    while (cursor != head && len < out_size) {
        /* Calculate contiguous span: either to head, to newline, or to buffer wrap */
        size_t span_end = (cursor < head) ? head : rb->size;
        size_t max_span = span_end - cursor;
        if (max_span > out_size - len) max_span = out_size - len;

        /* Search for newline in contiguous region */
        char *nl = memchr(rb->buffer + cursor, '\n', max_span);
        size_t span = nl ? (size_t)(nl - (rb->buffer + cursor) + 1) : max_span;

        memcpy(out + len, rb->buffer + cursor, span);
        len += span;
        cursor = (cursor + span) % rb->size;

        if (nl) break;  /* Found newline, line complete */
    }
    *cursor_out = cursor;
    return len;
}

static void ring_write(log_ring_t *rb, const char *data, size_t len)
{
    if (!rb || !rb->buffer || rb->size < 2 || !data || len == 0) return;

    /* Keep only the tail end if message longer than buffer */
    if (len >= rb->size) {
        data += (len - (rb->size - 1));
        len = rb->size - 1;
    }

    size_t used = ring_used(rb);
    size_t free_space = rb->size - used - 1;
    if (len > free_space) {
        size_t drop = len - free_space;
        rb->tail = (rb->tail + drop) % rb->size;
        /* Only reset broadcast_tail if it was pointing to dropped data.
         * If broadcast_tail is still in the valid range [new_tail, head],
         * keep it - that data hasn't been sent yet and is still valid.
         * Unconditionally resetting would cause already-sent data to be re-sent. */
        if (!cursor_in_range(rb->broadcast_tail, rb->tail, rb->head, rb->size)) {
            rb->broadcast_tail = rb->tail;
        }
    }

    size_t first = rb->size - rb->head;
    if (first > len) first = len;
    memcpy(rb->buffer + rb->head, data, first);
    if (len > first) {
        memcpy(rb->buffer, data + first, len - first);
    }
    rb->head = (rb->head + len) % rb->size;
}

static bool log_clients_add(int sock)
{
    if (!s_clients_mutex) return false;
    if (xSemaphoreTake(s_clients_mutex, pdMS_TO_TICKS(WC_MUTEX_TIMEOUT_MS)) != pdTRUE) {
        ESP_LOGE(TAG, "Clients mutex timeout in add");
        return false;
    }
    bool added = false;
    for (int i = 0; i < CONFIG_IAQ_WEB_CONSOLE_MAX_LOG_CLIENTS; ++i) {
        if (!s_clients[i].active) {
            s_clients[i].sock = sock;
            s_clients[i].active = true;
            s_clients[i].send_failures = 0;
            added = true;
            break;
        }
    }
    xSemaphoreGive(s_clients_mutex);
    return added;
}

static void log_clients_remove(int sock)
{
    if (!s_clients_mutex) return;
    if (xSemaphoreTake(s_clients_mutex, pdMS_TO_TICKS(WC_MUTEX_TIMEOUT_MS)) != pdTRUE) {
        ESP_LOGE(TAG, "Clients mutex timeout in remove");
        return;
    }
    for (int i = 0; i < CONFIG_IAQ_WEB_CONSOLE_MAX_LOG_CLIENTS; ++i) {
        if (s_clients[i].active && s_clients[i].sock == sock) {
            s_clients[i].active = false;
            s_clients[i].sock = -1;
            s_clients[i].send_failures = 0;
            break;
        }
    }
    xSemaphoreGive(s_clients_mutex);
}

void web_console_reset_log_state(void)
{
    if (!s_clients_mutex) return;
    bool locked = (xSemaphoreTake(s_clients_mutex, pdMS_TO_TICKS(WC_MUTEX_TIMEOUT_MS)) == pdTRUE);
    if (!locked) {
        ESP_LOGE(TAG, "Clients mutex timeout in reset");
    }
    for (int i = 0; i < CONFIG_IAQ_WEB_CONSOLE_MAX_LOG_CLIENTS; ++i) {
        s_clients[i].active = false;
        s_clients[i].sock = -1;
        s_clients[i].send_failures = 0;
    }
    if (locked) xSemaphoreGive(s_clients_mutex);
}

static void send_chunk_to_clients(const char *data, size_t len)
{
    httpd_handle_t server = web_console_get_server();
    if (!data || len == 0 || !server) return;

    httpd_ws_frame_t frame = {
        .type = HTTPD_WS_TYPE_TEXT,
        .payload = (uint8_t *)data,
        .len = len
    };

    if (!s_clients_mutex) return;
    if (xSemaphoreTake(s_clients_mutex, pdMS_TO_TICKS(WC_MUTEX_TIMEOUT_MS)) != pdTRUE) {
        ESP_LOGE(TAG, "Clients mutex timeout in send_chunk");
        return;
    }
    for (int i = 0; i < CONFIG_IAQ_WEB_CONSOLE_MAX_LOG_CLIENTS; ++i) {
        if (!s_clients[i].active) continue;
        esp_err_t err = httpd_ws_send_frame_async(server, s_clients[i].sock, &frame);
        if (err != ESP_OK) {
            s_clients[i].send_failures++;
            if (s_clients[i].send_failures >= 3) {
                ESP_LOGW(TAG, "Dropping log client %d (send failures)", s_clients[i].sock);
                /* Don't call httpd_sess_trigger_close - the session may already be
                 * closing (e.g., client disconnect). Just mark inactive and let
                 * httpd handle the socket cleanup. */
                s_clients[i].active = false;
            }
        } else {
            s_clients[i].send_failures = 0;
        }
    }
    xSemaphoreGive(s_clients_mutex);
}

static void dump_history_to_client(int sock, size_t history_end)
{
    httpd_handle_t server = web_console_get_server();
    if (!s_ring.buffer || s_ring.size == 0 || !s_ring_mutex || !server) return;

    uint64_t t0 = iaq_prof_tic();
    char *sendbuf = malloc(LOG_SEND_BATCH_SIZE);
    if (!sendbuf) {
        ESP_LOGW(TAG, "Cannot send log history: out of memory");
        iaq_prof_toc(IAQ_METRIC_WEB_CONSOLE_LOG_HISTORY, t0);
        return;
    }
    size_t send_len = 0;
    int send_failures = 0;
    bool aborted = false;

    if (xSemaphoreTake(s_ring_mutex, pdMS_TO_TICKS(WC_MUTEX_TIMEOUT_MS)) != pdTRUE) {
        ESP_LOGE(TAG, "Ring mutex timeout in history dump");
        free(sendbuf);
        iaq_prof_toc(IAQ_METRIC_WEB_CONSOLE_LOG_HISTORY, t0);
        return;
    }
    size_t cursor = s_ring.tail;
    size_t head = history_end;

    /* Read multiple lines under one lock until batch is full */
    while (cursor != head && !aborted) {
        /* Fill sendbuf with as many lines as fit */
        while (cursor != head && send_len < LOG_SEND_BATCH_SIZE) {
            size_t new_cursor;
            size_t line_len = ring_read_line(&s_ring, cursor, head,
                                              sendbuf + send_len,
                                              LOG_SEND_BATCH_SIZE - send_len,
                                              &new_cursor);
            if (line_len == 0) break;

            /* Ensure newline if line was truncated or incomplete */
            if (sendbuf[send_len + line_len - 1] != '\n') {
                if (send_len + line_len < LOG_SEND_BATCH_SIZE) {
                    sendbuf[send_len + line_len] = '\n';
                    line_len++;
                } else {
                    sendbuf[send_len + line_len - 1] = '\n';
                }
            }
            send_len += line_len;
            cursor = new_cursor;
        }
        xSemaphoreGive(s_ring_mutex);

        /* Send the batch */
        if (send_len > 0) {
            httpd_ws_frame_t frame = {
                .type = HTTPD_WS_TYPE_TEXT,
                .payload = (uint8_t *)sendbuf,
                .len = send_len
            };
            if (httpd_ws_send_frame_async(server, sock, &frame) != ESP_OK) {
                if (++send_failures >= 3) {
                    ESP_LOGW(TAG, "History dump aborted: too many send failures");
                    free(sendbuf);
                    iaq_prof_toc(IAQ_METRIC_WEB_CONSOLE_LOG_HISTORY, t0);
                    return;
                }
            }
            send_len = 0;
        }

        if (xSemaphoreTake(s_ring_mutex, pdMS_TO_TICKS(WC_MUTEX_TIMEOUT_MS)) != pdTRUE) {
            ESP_LOGE(TAG, "Ring mutex timeout in history dump loop");
            break;  /* Exit without mutex held */
        }
        /* Validate cursor is still in valid range after reacquiring mutex */
        if (!cursor_in_range(cursor, s_ring.tail, head, s_ring.size)) {
            xSemaphoreGive(s_ring_mutex);
            ESP_LOGW(TAG, "History dump aborted: ring overflow during send");
            aborted = true;
        }
    }
    if (!aborted) xSemaphoreGive(s_ring_mutex);

    if (send_len > 0) {
        httpd_ws_frame_t frame = {
            .type = HTTPD_WS_TYPE_TEXT,
            .payload = (uint8_t *)sendbuf,
            .len = send_len
        };
        (void)httpd_ws_send_frame_async(server, sock, &frame);
    }
    free(sendbuf);
    iaq_prof_toc(IAQ_METRIC_WEB_CONSOLE_LOG_HISTORY, t0);
}

static void log_broadcast_task(void *arg)
{
    (void)arg;
    uint8_t dummy;
    /* Static buffer to reduce stack usage.
     * Safe because only one broadcast task instance exists. */
    static char sendbuf[LOG_SEND_BATCH_SIZE];

    while (!s_exit_task) {
        if (xQueueReceive(s_notify_queue, &dummy, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        if (s_exit_task) break;
        if (!s_ring_mutex) continue;

        uint64_t t0 = iaq_prof_tic();
        size_t send_len = 0;

        if (xSemaphoreTake(s_ring_mutex, pdMS_TO_TICKS(WC_MUTEX_TIMEOUT_MS)) != pdTRUE) {
            ESP_LOGE(TAG, "Ring mutex timeout in broadcast task");
            continue;  /* Retry on next notification */
        }
        size_t cursor = s_ring.broadcast_tail;
        size_t head = s_ring.head;

        while (cursor != head) {
            /* Fill sendbuf with as many lines as fit under one lock */
            while (cursor != head && send_len < sizeof(sendbuf)) {
                size_t new_cursor;
                size_t line_len = ring_read_line(&s_ring, cursor, head,
                                                  sendbuf + send_len,
                                                  sizeof(sendbuf) - send_len,
                                                  &new_cursor);
                if (line_len == 0) break;

                /* Ensure newline if line was truncated */
                if (sendbuf[send_len + line_len - 1] != '\n') {
                    if (send_len + line_len < sizeof(sendbuf)) {
                        sendbuf[send_len + line_len] = '\n';
                        line_len++;
                    } else {
                        sendbuf[send_len + line_len - 1] = '\n';
                    }
                }
                send_len += line_len;
                cursor = new_cursor;
            }

            /* Update broadcast_tail before releasing mutex */
            s_ring.broadcast_tail = cursor;
            xSemaphoreGive(s_ring_mutex);

            /* Send the batch */
            if (send_len > 0) {
                send_chunk_to_clients(sendbuf, send_len);
                send_len = 0;
            }

            if (xSemaphoreTake(s_ring_mutex, pdMS_TO_TICKS(WC_MUTEX_TIMEOUT_MS)) != pdTRUE) {
                ESP_LOGE(TAG, "Ring mutex timeout in broadcast loop");
                goto next_cycle;  /* Exit loop, will retry on next notification */
            }
            head = s_ring.head;

            /* Check if ring overflowed during send - broadcast_tail would have
             * been advanced by ring_write. If so, skip to the new position. */
            if (s_ring.broadcast_tail != cursor) {
                ESP_LOGD(TAG, "Ring overflow during broadcast; skipping to new tail");
                cursor = s_ring.broadcast_tail;
            }
        }

        s_ring.broadcast_tail = cursor;
        xSemaphoreGive(s_ring_mutex);

next_cycle:
        iaq_prof_toc(IAQ_METRIC_WEB_CONSOLE_LOG_BROADCAST, t0);
    }
    vTaskDelete(NULL);
}

/* Linker-wrapped _write_r to tee stdout/stderr into log ring.
 * Captures from very first printf/ESP_LOG - early boot is single-threaded so no mutex needed. */
extern int __real__write_r(struct _reent *r, int fd, const void *data, size_t size);

int __wrap__write_r(struct _reent *r, int fd, const void *data, size_t size)
{
    /* Call the original _write_r first to preserve behavior. */
    ssize_t ret = __real__write_r(r, fd, data, size);

    /* Tee stdout/stderr into ring buffer. */
    if (fd == STDOUT_FILENO || fd == STDERR_FILENO) {
        /* Early boot (before mutex created): single-threaded, write directly.
         * After init: take mutex to synchronize with broadcast task. */
        if (!s_ring_mutex || xSemaphoreTake(s_ring_mutex, pdMS_TO_TICKS(5)) == pdTRUE) {
            ring_write(&s_ring, (const char *)data, size);
            if (s_ring_mutex) xSemaphoreGive(s_ring_mutex);
        }
        if (s_notify_queue && uxQueueMessagesWaiting(s_notify_queue) == 0) {
            uint8_t dummy = 1;
            (void)xQueueSend(s_notify_queue, &dummy, 0);
        }
    }
    return (int)ret;
}

esp_err_t web_console_log_init(void)
{
    if (s_ring_mutex) return ESP_OK;

    s_ring_mutex = xSemaphoreCreateMutex();
    s_clients_mutex = xSemaphoreCreateMutex();
    s_notify_queue = xQueueCreate(4, sizeof(uint8_t));
    if (!s_ring_mutex || !s_clients_mutex || !s_notify_queue) {
        ESP_LOGE(TAG, "Failed to create log primitives");
        web_console_log_stop();
        return ESP_ERR_NO_MEM;
    }

    s_exit_task = false;
    BaseType_t rc = xTaskCreatePinnedToCore(log_broadcast_task, "wc_log_bcast",
                                            TASK_STACK_WC_LOG_BCAST, NULL,
                                            TASK_PRIORITY_WC_LOG_BCAST, &s_broadcast_task,
                                            TASK_CORE_WC_LOG_BCAST);
    if (rc != pdPASS) {
        ESP_LOGE(TAG, "Failed to start log broadcast task");
        web_console_log_stop();
        return ESP_ERR_NO_MEM;
    }
    iaq_profiler_register_task("wc_log_bcast", s_broadcast_task, TASK_STACK_WC_LOG_BCAST);

    ESP_LOGI(TAG, "Web console log capture installed (buf=%d bytes)", CONFIG_IAQ_WEB_CONSOLE_LOG_BUFFER_SIZE);
    return ESP_OK;
}

void web_console_log_stop(void)
{
    s_exit_task = true;
    if (s_notify_queue) {
        uint8_t dummy = 1;
        (void)xQueueSend(s_notify_queue, &dummy, 0);
    }
    if (s_broadcast_task) {
        iaq_profiler_unregister_task(s_broadcast_task);
        /* Give task a chance to exit on its own after the wakeup */
        for (int i = 0; i < 20; ++i) {
            if (eTaskGetState(s_broadcast_task) == eDeleted) break;
            vTaskDelay(pdMS_TO_TICKS(10));
        }
        if (eTaskGetState(s_broadcast_task) != eDeleted) {
            vTaskDelete(s_broadcast_task);
        }
        s_broadcast_task = NULL;
    }
    /* Reset ring pointers but keep static buffer (allows re-init). */
    s_ring.head = s_ring.tail = s_ring.broadcast_tail = 0;
    if (s_ring_mutex) { vSemaphoreDelete(s_ring_mutex); s_ring_mutex = NULL; }
    if (s_clients_mutex) { vSemaphoreDelete(s_clients_mutex); s_clients_mutex = NULL; }
    if (s_notify_queue) { vQueueDelete(s_notify_queue); s_notify_queue = NULL; }
}

static esp_err_t ws_log_handler(httpd_req_t *req)
{
    int sock = httpd_req_to_sockfd(req);
    if (req->method == HTTP_GET) {
        if (!web_console_check_auth(req)) {
            httpd_resp_set_status(req, "401 Unauthorized");
            (void)httpd_resp_send(req, NULL, 0);
            return ESP_FAIL;
        }

        /* Capture current head as history boundary BEFORE adding client.
         * This ensures no overlap or gaps with the broadcast task. */
        size_t history_end = 0;
        if (xSemaphoreTake(s_ring_mutex, pdMS_TO_TICKS(WC_MUTEX_TIMEOUT_MS)) == pdTRUE) {
            history_end = s_ring.head;
            xSemaphoreGive(s_ring_mutex);
        } else {
            ESP_LOGW(TAG, "Ring mutex timeout; skipping history");
        }

        /* Add client to list BEFORE sending history, so broadcast task
         * will include this client for any new logs that arrive. */
        if (!log_clients_add(sock)) {
            httpd_ws_frame_t closefrm = { .type = HTTPD_WS_TYPE_CLOSE };
            (void)httpd_ws_send_frame(req, &closefrm);
            return ESP_FAIL;
        }

        /* Send history up to the captured point only. */
        dump_history_to_client(sock, history_end);

        ESP_LOGI(TAG, "Log client connected: %d", sock);
        return ESP_OK;
    }

    httpd_ws_frame_t frame = {0};
    esp_err_t ret = httpd_ws_recv_frame(req, &frame, 0);
    if (ret != ESP_OK) {
        log_clients_remove(sock);
        return ret;
    }

    /* Log endpoint only handles control frames; cap allocation to prevent abuse.
     * RFC 6455 limits control frame payloads to 125 bytes. */
    size_t alloc_len = frame.len;
    if (alloc_len > 125) alloc_len = 125;

    if (alloc_len) {
        uint8_t *buf = malloc(alloc_len + 1);
        if (!buf) return ESP_ERR_NO_MEM;
        frame.payload = buf;
        ret = httpd_ws_recv_frame(req, &frame, alloc_len);
        if (ret == ESP_OK && frame.type == HTTPD_WS_TYPE_PING) {
            httpd_ws_frame_t pong = { .type = HTTPD_WS_TYPE_PONG, .payload = frame.payload, .len = frame.len };
            (void)httpd_ws_send_frame(req, &pong);
        }
        free(buf);
    } else if (frame.type == HTTPD_WS_TYPE_PING) {
        httpd_ws_frame_t pong = { .type = HTTPD_WS_TYPE_PONG, .payload = NULL, .len = 0 };
        (void)httpd_ws_send_frame(req, &pong);
    }

    if (frame.type == HTTPD_WS_TYPE_CLOSE || ret != ESP_OK) {
        log_clients_remove(sock);
        httpd_ws_frame_t closefrm = { .type = HTTPD_WS_TYPE_CLOSE };
        (void)httpd_ws_send_frame(req, &closefrm);
        ESP_LOGI(TAG, "Log client closed: %d", sock);
    }
    return ret;
}

const httpd_uri_t web_console_uri_log = {
    .uri = "/ws/log",
    .method = HTTP_GET,
    .handler = ws_log_handler,
    .is_websocket = true,
    .handle_ws_control_frames = true,
};
