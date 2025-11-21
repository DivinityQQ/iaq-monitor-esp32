/* components/web_portal/web_portal.c */
#include <string.h>
#include <sys/stat.h>
#include <sys/param.h>
#include <strings.h>
#include "esp_log.h"
#include "esp_littlefs.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_chip_info.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "esp_http_server.h"
#include "esp_https_server.h"
#include "sdkconfig.h"
#include "esp_netif.h"
#include "lwip/inet.h"

#include "iaq_config.h"
#include "iaq_data.h"
#include "iaq_json.h"
#include "wifi_manager.h"
#include "mqtt_manager.h"
#include "sensor_coordinator.h"
#include "system_context.h"
#include "dns_server.h"
#include "esp_wifi.h"
#include "web_portal.h"
#include "iaq_profiler.h"
#include "pm_guard.h"

static const char *TAG = "WEB_PORTAL";

/* LittleFS mount base */
#define WEB_MOUNT_POINT "/www"

/* WebSocket state */
#ifndef CONFIG_IAQ_WEB_PORTAL_MAX_WS_CLIENTS
#define CONFIG_IAQ_WEB_PORTAL_MAX_WS_CLIENTS 8
#endif
#define MAX_WS_CLIENTS CONFIG_IAQ_WEB_PORTAL_MAX_WS_CLIENTS
typedef struct {
    int sock;  /* socket fd */
    bool active;
    int64_t last_pong_us;
} ws_client_t;

/* ws_async_arg removed (no longer needed) */

static httpd_handle_t s_server = NULL;
static bool s_server_is_https = false;
static volatile bool s_portal_restart_pending = false;
static iaq_system_context_t *s_ctx = NULL;
static ws_client_t s_ws_clients[MAX_WS_CLIENTS];
static SemaphoreHandle_t s_ws_mutex;
static TaskHandle_t s_httpd_task_handle = NULL;

static esp_timer_handle_t s_ws_state_timer = NULL;
static esp_timer_handle_t s_ws_metrics_timer = NULL;
static esp_timer_handle_t s_ws_health_timer = NULL;
static bool s_ws_timers_running = false;
static dns_server_handle_t s_dns = NULL;

/* Forward declarations */
static bool web_portal_should_use_https(void);
static void web_portal_restart_task(void *arg);
/* no-op */

/* Forward declarations for captive portal helpers registered later */
static void dhcp_set_captiveportal_uri(void);
static esp_err_t http_404_error_handler(httpd_req_t *req, httpd_err_code_t err);

/* Helpers */
static void ws_clients_init(void)
{
    memset(s_ws_clients, 0, sizeof(s_ws_clients));
    if (!s_ws_mutex) s_ws_mutex = xSemaphoreCreateMutex();
    if (!s_ws_mutex) {
        ESP_LOGE(TAG, "WS: failed to create mutex");
    }
}

static bool ws_clients_add(int sock)
{
    if (!s_ws_mutex) return false;
    xSemaphoreTake(s_ws_mutex, portMAX_DELAY);
    bool need_start = false;
    bool added = false;
    for (int i = 0; i < MAX_WS_CLIENTS; ++i) {
        if (!s_ws_clients[i].active) {
            s_ws_clients[i].sock = sock;
            s_ws_clients[i].active = true;
            s_ws_clients[i].last_pong_us = esp_timer_get_time();
            added = true;
            break;
        }
    }
    int active = 0;
    for (int i = 0; i < MAX_WS_CLIENTS; ++i) {
        if (s_ws_clients[i].active) active++;
    }
    if ((active == 1) && !s_ws_timers_running) {
        need_start = true;
        s_ws_timers_running = true;
    }
    xSemaphoreGive(s_ws_mutex);
    if (need_start) {
        ESP_LOGI(TAG, "WS: first client, starting timers");
        ESP_ERROR_CHECK_WITHOUT_ABORT(esp_timer_start_periodic(s_ws_state_timer, 1000 * 1000));
        ESP_ERROR_CHECK_WITHOUT_ABORT(esp_timer_start_periodic(s_ws_metrics_timer, 5 * 1000 * 1000));
        ESP_ERROR_CHECK_WITHOUT_ABORT(esp_timer_start_periodic(s_ws_health_timer, 1 * 1000 * 1000));
    }
    return added;
}

static void ws_clients_remove(int sock)
{
    if (!s_ws_mutex) return;
    xSemaphoreTake(s_ws_mutex, portMAX_DELAY);
    for (int i = 0; i < MAX_WS_CLIENTS; ++i) {
        if (s_ws_clients[i].active && s_ws_clients[i].sock == sock) {
            s_ws_clients[i].active = false;
            s_ws_clients[i].sock = -1;
            s_ws_clients[i].last_pong_us = 0;
            /* Ask httpd to close the session (safe from any task) */
            if (s_server && sock >= 0) {
                httpd_sess_trigger_close(s_server, sock);
            }
            break;
        }
    }
    int active = 0;
    for (int i = 0; i < MAX_WS_CLIENTS; ++i) {
        if (s_ws_clients[i].active) active++;
    }
    bool need_stop = (active == 0) && s_ws_timers_running;
    if (need_stop) s_ws_timers_running = false;
    xSemaphoreGive(s_ws_mutex);
    if (need_stop) {
        ESP_LOGI(TAG, "WS: last client gone, stopping timers");
        ESP_ERROR_CHECK_WITHOUT_ABORT(esp_timer_stop(s_ws_state_timer));
        ESP_ERROR_CHECK_WITHOUT_ABORT(esp_timer_stop(s_ws_metrics_timer));
        ESP_ERROR_CHECK_WITHOUT_ABORT(esp_timer_stop(s_ws_health_timer));
    }
}

/* async per-client sender removed; send directly via httpd_ws_send_frame_async */

static void ws_broadcast_json(const char *type, cJSON *payload)
{
    if (!s_server || !payload) { if (payload) cJSON_Delete(payload); return; }
    uint64_t t0 = iaq_prof_tic();

    pm_guard_lock_cpu();
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "type", type);
    cJSON_AddItemToObject(root, "data", payload); /* takes ownership */
    char *txt = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!txt) {
        pm_guard_unlock_cpu();
        return;
    }

    if (s_ws_mutex) xSemaphoreTake(s_ws_mutex, portMAX_DELAY);
    for (int i = 0; i < MAX_WS_CLIENTS; ++i) {
        if (!s_ws_clients[i].active) continue;
        int sock = s_ws_clients[i].sock;
        if (httpd_ws_get_fd_info(s_server, sock) != HTTPD_WS_CLIENT_WEBSOCKET) {
            ESP_LOGD(TAG, "WS fd %d not in WEBSOCKET state during broadcast; removing", sock);
            int old = s_ws_clients[i].sock;
            s_ws_clients[i].active = false;
            s_ws_clients[i].sock = -1;
            s_ws_clients[i].last_pong_us = 0;
            /* Proactively close the underlying HTTPD session to free socket */
            if (s_server && old >= 0) {
                httpd_sess_trigger_close(s_server, old);
            }
            continue;
        }
        httpd_ws_frame_t frame = { 0 };
        frame.type = HTTPD_WS_TYPE_TEXT;
        frame.payload = (uint8_t*)txt;
        frame.len = strlen(txt);
        esp_err_t er = httpd_ws_send_frame_async(s_server, sock, &frame);
        if (er != ESP_OK) {
            ESP_LOGW(TAG, "WS: enqueue broadcast to %d failed: %s", sock, esp_err_to_name(er));
        }
    }
    if (s_ws_mutex) xSemaphoreGive(s_ws_mutex);

    pm_guard_unlock_cpu();
    free(txt);
    iaq_prof_toc(IAQ_METRIC_WEB_WS_BROADCAST, t0);
}

/* Send a single JSON envelope to a specific WS fd (async on httpd task) */
static void ws_send_json_to_fd(int fd, const char *type, cJSON *payload)
{
    if (!s_server || !payload) { if (payload) cJSON_Delete(payload); return; }
    pm_guard_lock_cpu();
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "type", type);
    cJSON_AddItemToObject(root, "data", payload); /* takes ownership */
    char *txt = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!txt) {
        pm_guard_unlock_cpu();
        return;
    }

    httpd_ws_frame_t frame = (httpd_ws_frame_t){ 0 };
    frame.type = HTTPD_WS_TYPE_TEXT;
    frame.payload = (uint8_t*)txt;
    frame.len = strlen(txt);
    esp_err_t er = httpd_ws_send_frame_async(s_server, fd, &frame);
    if (er != ESP_OK) {
        ESP_LOGW(TAG, "WS: enqueue send to %d failed: %s", fd, esp_err_to_name(er));
    }
    pm_guard_unlock_cpu();
    free(txt);
}

/* Timers to push live data (offload work to HTTP server task) */
static void ws_work_send_state(void *arg) { (void)arg; iaq_data_t snap = (iaq_data_t){0}; IAQ_DATA_WITH_LOCK(){ snap = *iaq_data_get(); } ws_broadcast_json("state", iaq_json_build_state(&snap)); }
static void ws_work_send_metrics(void *arg) { (void)arg; iaq_data_t snap = (iaq_data_t){0}; IAQ_DATA_WITH_LOCK(){ snap = *iaq_data_get(); } ws_broadcast_json("metrics", iaq_json_build_metrics(&snap)); }
static void ws_work_send_health(void *arg) { (void)arg; iaq_data_t snap = (iaq_data_t){0}; IAQ_DATA_WITH_LOCK(){ snap = *iaq_data_get(); } ws_broadcast_json("health", iaq_json_build_health(&snap)); }

static void ws_state_timer_cb(void *arg) { (void)arg; if (s_server) { esp_err_t er = httpd_queue_work(s_server, ws_work_send_state, NULL); if (er != ESP_OK) ESP_LOGW(TAG, "WS: queue state failed: %s", esp_err_to_name(er)); } }
static void ws_metrics_timer_cb(void *arg) { (void)arg; if (s_server) { esp_err_t er = httpd_queue_work(s_server, ws_work_send_metrics, NULL); if (er != ESP_OK) ESP_LOGW(TAG, "WS: queue metrics failed: %s", esp_err_to_name(er)); } }

static void ws_ping_and_prune(void)
{
    const int64_t now = esp_timer_get_time();
    if (!s_ws_mutex) return;
    xSemaphoreTake(s_ws_mutex, portMAX_DELAY);
    for (int i = 0; i < MAX_WS_CLIENTS; ++i) {
        if (!s_ws_clients[i].active) continue;
        int sock = s_ws_clients[i].sock;
        if (httpd_ws_get_fd_info(s_server, sock) != HTTPD_WS_CLIENT_WEBSOCKET) {
            ESP_LOGD(TAG, "WS fd %d not in WEBSOCKET state during ping; removing", sock);
            s_ws_clients[i].active = false;
            s_ws_clients[i].sock = -1;
            s_ws_clients[i].last_pong_us = 0;
            /* Also close the HTTPD session to reclaim socket */
            if (s_server && sock >= 0) {
                httpd_sess_trigger_close(s_server, sock);
            }
            continue;
        }
        int64_t last = s_ws_clients[i].last_pong_us;
        if (last > 0 && (now - last) > ((int64_t)CONFIG_IAQ_WEB_PORTAL_WS_PONG_TIMEOUT_SEC * 1000000LL)) {
            ESP_LOGW(TAG, "WS: client %d stale (> %ds), removing", sock, CONFIG_IAQ_WEB_PORTAL_WS_PONG_TIMEOUT_SEC);
            s_ws_clients[i].active = false;
            s_ws_clients[i].sock = -1;
            s_ws_clients[i].last_pong_us = 0;
            /* Close the underlying session so resources are freed promptly */
            if (s_server && sock >= 0) {
                httpd_sess_trigger_close(s_server, sock);
            }
            continue;
        }
        httpd_ws_frame_t ping = { 0 };
        ping.type = HTTPD_WS_TYPE_PING;
        ping.payload = (uint8_t*)"ping";
        ping.len = 4;
        esp_err_t pr = httpd_ws_send_frame_async(s_server, sock, &ping);
        if (pr != ESP_OK) {
            ESP_LOGW(TAG, "WS: failed to enqueue PING to %d: %s", sock, esp_err_to_name(pr));
        } else {
            ESP_LOGD(TAG, "WS: sent PING to %d", sock);
        }
    }
    xSemaphoreGive(s_ws_mutex);
}

static void ws_health_timer_cb(void *arg)
{
    (void)arg;
    if (!s_server) return;
    {
        esp_err_t er = httpd_queue_work(s_server, ws_work_send_health, NULL);
        if (er != ESP_OK) ESP_LOGW(TAG, "WS: queue health failed: %s", esp_err_to_name(er));
    }
    /* Send WS PINGs at configured interval regardless of health period */
    static int secs_since_ping = 0;
    secs_since_ping += 1; /* health timer runs at 1 Hz */
    if (secs_since_ping >= CONFIG_IAQ_WEB_PORTAL_WS_PING_INTERVAL_SEC) {
        secs_since_ping = 0;
        ws_ping_and_prune();
    }
}

/* Coalesced health push: schedule a one-shot send a short time in the future
 * to batch multiple state changes into a single snapshot. */
/* No coalescing needed with 1 Hz health updates */


/* Utilities */
static inline void set_cors(httpd_req_t *req)
{
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", CONFIG_IAQ_WEB_PORTAL_CORS_ORIGIN);
    httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "GET,POST,OPTIONS");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "Content-Type");
}

static void set_status_code(httpd_req_t *req, int status)
{
    switch (status) {
        case 200: httpd_resp_set_status(req, "200 OK"); break;
        case 201: httpd_resp_set_status(req, "201 Created"); break;
        case 204: httpd_resp_set_status(req, "204 No Content"); break;
        case 400: httpd_resp_set_status(req, "400 Bad Request"); break;
        case 401: httpd_resp_set_status(req, "401 Unauthorized"); break;
        case 403: httpd_resp_set_status(req, "403 Forbidden"); break;
        case 404: httpd_resp_set_status(req, "404 Not Found"); break;
        case 409: httpd_resp_set_status(req, "409 Conflict"); break;
        case 500: httpd_resp_set_status(req, "500 Internal Server Error"); break;
        default:  httpd_resp_set_status(req, "400 Bad Request"); break;
    }
}

static void respond_json(httpd_req_t *req, cJSON *obj, int status)
{
    char *s = iaq_json_to_string_and_delete(obj);
    if (!s) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "json");
        return;
    }
    set_cors(req);
    set_status_code(req, status);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, s);
    free(s);
}

static void respond_error(httpd_req_t *req, int status, const char *code, const char *message)
{
    cJSON *root = cJSON_CreateObject();
    cJSON *err = cJSON_CreateObject();
    cJSON_AddStringToObject(err, "code", code ? code : "ERR");
    cJSON_AddStringToObject(err, "message", message ? message : "");
    cJSON_AddNumberToObject(err, "status", status);
    cJSON_AddItemToObject(root, "error", err);
    respond_json(req, root, status);
}

static esp_err_t api_options_handler(httpd_req_t *req)
{
    set_cors(req);
    httpd_resp_set_status(req, "204 No Content");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

static bool read_req_json(httpd_req_t *req, cJSON **out)
{
    int total = req->content_len;
    if (total <= 0 || total > 4096) return false;
    char *buf = (char *)malloc(total + 1);
    if (!buf) return false;
    int recvd = 0;
    while (recvd < total) {
        int r = httpd_req_recv(req, buf + recvd, total - recvd);
        if (r <= 0) { free(buf); return false; }
        recvd += r;
    }
    buf[recvd] = '\0';
    cJSON *root = cJSON_Parse(buf);
    free(buf);
    if (!root) return false;
    *out = root;
    return true;
}

/* ===== Static file server ===== */
static const char* guess_mime_type(const char *path)
{
    const char *ext = strrchr(path, '.');
    if (!ext) return "text/plain";
    if (!strcasecmp(ext, ".html") || !strcasecmp(ext, ".htm")) return "text/html";
    if (!strcasecmp(ext, ".css")) return "text/css";
    if (!strcasecmp(ext, ".js")) return "application/javascript";
    if (!strcasecmp(ext, ".json")) return "application/json";
    if (!strcasecmp(ext, ".svg")) return "image/svg+xml";
    if (!strcasecmp(ext, ".png")) return "image/png";
    if (!strcasecmp(ext, ".jpg") || !strcasecmp(ext, ".jpeg")) return "image/jpeg";
    if (!strcasecmp(ext, ".woff2")) return "font/woff2";
    if (!strcasecmp(ext, ".wasm")) return "application/wasm";
    if (!strcasecmp(ext, ".ico")) return "image/x-icon";
    return "application/octet-stream";
}

static esp_err_t static_handler(httpd_req_t *req)
{
    uint64_t t0 = iaq_prof_tic();
    char path[CONFIG_HTTPD_MAX_URI_LEN + 32];
    const char *uri = req->uri;
    /* Path traversal guard */
    if (!uri || uri[0] != '/' || strstr(uri, "..") != NULL || strchr(uri, '\\') != NULL) {
        respond_error(req, 400, "BAD_PATH", "Invalid path");
        iaq_prof_toc(IAQ_METRIC_WEB_STATIC, t0);
        return ESP_OK;
    }
    if (strcmp(uri, "/") == 0) uri = "/index.html"; /* SPA entry */
    snprintf(path, sizeof(path), WEB_MOUNT_POINT "%s", uri);

    /* Determine if client accepts gzip */
    bool client_accepts_gzip = false;
    size_t ae_len = httpd_req_get_hdr_value_len(req, "Accept-Encoding");
    if (ae_len > 0 && ae_len < 256) {
        char ae[256];
        if (httpd_req_get_hdr_value_str(req, "Accept-Encoding", ae, sizeof(ae)) == ESP_OK) {
            if (strstr(ae, "gzip") != NULL) client_accepts_gzip = true;
        }
    }

    /* Prefer precompressed .gz variant when supported and available */
    char gz_path[sizeof(path) + 4];
    snprintf(gz_path, sizeof(gz_path), "%s.gz", path);

    const char *serve_path = path; /* default */
    bool serve_gzip = false;
    bool serve_fallback_html = false; /* SPA history fallback */

    struct stat st_orig;
    struct stat st_gz;

    bool orig_ok = (stat(path, &st_orig) == 0 && st_orig.st_size > 0);
    bool gz_ok = false;
    if (client_accepts_gzip) {
        gz_ok = (stat(gz_path, &st_gz) == 0 && st_gz.st_size > 0);
        if (gz_ok) {
            serve_path = gz_path;
            serve_gzip = true;
        }
    }

    /* If file not found, implement SPA history API fallback: serve index.html
     * for navigation requests (paths with no extension), so `/config` reloads
     * load the app instead of 404. Keep 404 for missing assets. */
    if (!orig_ok && !serve_gzip) {
        bool has_dot = (strchr(uri, '.') != NULL);
        if (!has_dot) {
            /* Fallback to /index.html (or .gz if available and accepted) */
            static char index_path[sizeof(WEB_MOUNT_POINT) + 16];
            static char index_gz_path[sizeof(WEB_MOUNT_POINT) + 20];
            snprintf(index_path, sizeof(index_path), WEB_MOUNT_POINT "/index.html");
            snprintf(index_gz_path, sizeof(index_gz_path), WEB_MOUNT_POINT "/index.html.gz");
            struct stat st_idx;
            struct stat st_idx_gz;
            bool idx_ok = (stat(index_path, &st_idx) == 0 && st_idx.st_size > 0);
            bool idx_gz_ok = false;
            if (client_accepts_gzip) {
                idx_gz_ok = (stat(index_gz_path, &st_idx_gz) == 0 && st_idx_gz.st_size > 0);
            }
            if (idx_gz_ok) {
                serve_path = index_gz_path;
                serve_gzip = true;
                serve_fallback_html = true;
            } else if (idx_ok) {
                serve_path = index_path;
                serve_gzip = false;
                serve_fallback_html = true;
            } else {
                /* No index.html available: return 404 as before */
                respond_error(req, 404, "NOT_FOUND", "Resource not found");
                iaq_prof_toc(IAQ_METRIC_WEB_STATIC, t0);
                return ESP_OK;
            }
        } else {
            /* Asset-like path missing -> 404 */
            respond_error(req, 404, "NOT_FOUND", "Resource not found");
            iaq_prof_toc(IAQ_METRIC_WEB_STATIC, t0);
            return ESP_OK;
        }
    }

    FILE *f = fopen(serve_path, "rb");
    if (!f) {
        respond_error(req, 500, "OPEN_FAILED", "Failed to open file");
        iaq_prof_toc(IAQ_METRIC_WEB_STATIC, t0);
        return ESP_OK;
    }

    /* Set content type: force text/html for SPA fallback */
    if (serve_fallback_html) {
        httpd_resp_set_type(req, "text/html");
    } else {
        /* Based on original (non-.gz) extension */
        httpd_resp_set_type(req, guess_mime_type(path));
    }
    if (serve_gzip) {
        httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
        httpd_resp_set_hdr(req, "Vary", "Accept-Encoding");
    }

    /* Caching: long TTL for hashed assets, no-cache for HTML, default otherwise */
    char cc[64];
    const char *ext = strrchr(uri, '.');
    bool is_html = serve_fallback_html || (ext && (!strcasecmp(ext, ".html") || !strcasecmp(ext, ".htm"))) || strcmp(uri, "/") == 0;
    bool is_asset = (strncmp(uri, "/assets/", 8) == 0);
    if (is_asset) {
        snprintf(cc, sizeof(cc), "public, max-age=%d, immutable", 31536000); /* 1 year */
    } else if (is_html) {
        snprintf(cc, sizeof(cc), "no-cache");
    } else {
        snprintf(cc, sizeof(cc), "public, max-age=%d", CONFIG_IAQ_WEB_PORTAL_STATIC_MAX_AGE_SEC);
    }
    httpd_resp_set_hdr(req, "Cache-Control", cc);

    char buf[1024];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
        if (httpd_resp_send_chunk(req, buf, n) != ESP_OK) break;
    }
    fclose(f);
    httpd_resp_send_chunk(req, NULL, 0);
    iaq_prof_toc(IAQ_METRIC_WEB_STATIC, t0);
    return ESP_OK;
}

/* ===== API handlers ===== */
static esp_err_t api_info_get(httpd_req_t *req)
{
    iaq_data_t snap = {0}; IAQ_DATA_WITH_LOCK() { snap = *iaq_data_get(); }
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "device_id", CONFIG_IAQ_DEVICE_ID);

    /* Device metadata */
    cJSON *device = cJSON_CreateObject();
    cJSON_AddStringToObject(device, "name", "IAQ Monitor");
    cJSON_AddStringToObject(device, "model", "ESP32-S3 DIY");
    cJSON_AddStringToObject(device, "manufacturer", "Homemade");
    cJSON_AddItemToObject(root, "device", device);

    /* Firmware information */
    cJSON *firmware = cJSON_CreateObject();
    char ver_str[16];
    snprintf(ver_str, sizeof(ver_str), "%d.%d.%d", IAQ_VERSION_MAJOR, IAQ_VERSION_MINOR, IAQ_VERSION_PATCH);
    cJSON_AddStringToObject(firmware, "version", ver_str);
    cJSON_AddStringToObject(firmware, "build_date", __DATE__);
    cJSON_AddStringToObject(firmware, "build_time", __TIME__);
    cJSON_AddStringToObject(firmware, "idf_version", esp_get_idf_version());
    cJSON_AddStringToObject(firmware, "license", "Apache-2.0");
    cJSON_AddItemToObject(root, "firmware", firmware);

    /* Hardware information */
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);
    cJSON *hardware = cJSON_CreateObject();
    const char *chip_model = "Unknown";
    switch (chip_info.model) {
        case CHIP_ESP32: chip_model = "ESP32"; break;
        case CHIP_ESP32S2: chip_model = "ESP32-S2"; break;
        case CHIP_ESP32S3: chip_model = "ESP32-S3"; break;
        case CHIP_ESP32C3: chip_model = "ESP32-C3"; break;
        case CHIP_ESP32C2: chip_model = "ESP32-C2"; break;
        case CHIP_ESP32C6: chip_model = "ESP32-C6"; break;
        case CHIP_ESP32H2: chip_model = "ESP32-H2"; break;
        default: break;
    }
    cJSON_AddStringToObject(hardware, "chip", chip_model);
    cJSON_AddNumberToObject(hardware, "cores", chip_info.cores);
    cJSON_AddNumberToObject(hardware, "revision", chip_info.revision);
    cJSON_AddItemToObject(root, "hardware", hardware);

    /* Network summary */
    cJSON *net = cJSON_CreateObject();
    wifi_mode_t mode = wifi_manager_get_mode();
    const char *mode_str = (mode == WIFI_MODE_STA) ? "STA" : (mode == WIFI_MODE_AP) ? "AP" : (mode == WIFI_MODE_APSTA) ? "APSTA" : "OFF";
    cJSON_AddStringToObject(net, "mode", mode_str);
    cJSON_AddBoolToObject(net, "wifi_connected", snap.system.wifi_connected);
    cJSON_AddBoolToObject(net, "mqtt_connected", snap.system.mqtt_connected);
    // IPs
    esp_netif_ip_info_t ipi; char ipbuf[16];
    esp_netif_t *sta = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (sta && esp_netif_get_ip_info(sta, &ipi) == ESP_OK) { inet_ntoa_r(ipi.ip.addr, ipbuf, sizeof(ipbuf)); cJSON_AddStringToObject(net, "sta_ip", ipbuf); }
    esp_netif_t *ap = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
    if (ap && esp_netif_get_ip_info(ap, &ipi) == ESP_OK) { inet_ntoa_r(ipi.ip.addr, ipbuf, sizeof(ipbuf)); cJSON_AddStringToObject(net, "ap_ip", ipbuf); }
    cJSON_AddItemToObject(root, "network", net);
    respond_json(req, root, 200);
    return ESP_OK;
}

static esp_err_t api_state_get(httpd_req_t *req)
{
    uint64_t t0 = iaq_prof_tic();
    iaq_data_t s = (iaq_data_t){0}; IAQ_DATA_WITH_LOCK(){s=*iaq_data_get();}
    respond_json(req, iaq_json_build_state(&s), 200);
    iaq_prof_toc(IAQ_METRIC_WEB_API_STATE, t0);
    return ESP_OK;
}

static esp_err_t api_metrics_get(httpd_req_t *req)
{
    uint64_t t0 = iaq_prof_tic();
    iaq_data_t s = (iaq_data_t){0}; IAQ_DATA_WITH_LOCK(){s=*iaq_data_get();}
    respond_json(req, iaq_json_build_metrics(&s), 200);
    iaq_prof_toc(IAQ_METRIC_WEB_API_METRICS, t0);
    return ESP_OK;
}

static esp_err_t api_health_get(httpd_req_t *req)
{
    uint64_t t0 = iaq_prof_tic();
    iaq_data_t s = (iaq_data_t){0}; IAQ_DATA_WITH_LOCK(){s=*iaq_data_get();}
    respond_json(req, iaq_json_build_health(&s), 200);
    iaq_prof_toc(IAQ_METRIC_WEB_API_HEALTH, t0);
    return ESP_OK;
}

static esp_err_t api_wifi_get(httpd_req_t *req)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "provisioned", wifi_manager_is_provisioned());
    wifi_mode_t mode = wifi_manager_get_mode();
    const char *mode_str = (mode == WIFI_MODE_STA) ? "STA" : (mode == WIFI_MODE_AP) ? "AP" : (mode == WIFI_MODE_APSTA) ? "APSTA" : "OFF";
    cJSON_AddStringToObject(root, "mode", mode_str);
    char ssid[33] = {0};
    wifi_manager_get_ssid(ssid, sizeof(ssid));
    cJSON_AddStringToObject(root, "ssid", ssid);
    cJSON_AddNumberToObject(root, "rssi", wifi_manager_get_rssi());
    respond_json(req, root, 200);
    return ESP_OK;
}

static esp_err_t api_sensors_cadence_get(httpd_req_t *req)
{
    uint64_t t0 = iaq_prof_tic();
    uint32_t ms[SENSOR_ID_MAX] = {0};
    bool from_nvs[SENSOR_ID_MAX] = {0};
    esp_err_t r = sensor_coordinator_get_cadences(ms, from_nvs);
    if (r != ESP_OK) { respond_error(req, 500, esp_err_to_name(r), "Failed to read cadences"); iaq_prof_toc(IAQ_METRIC_WEB_API_SENSORS, t0); return ESP_OK; }
    cJSON *root = cJSON_CreateObject();
    cJSON *cad = cJSON_CreateObject();
    cJSON_AddItemToObject(root, "cadences", cad);
    const char *names[SENSOR_ID_MAX] = { "mcu", "sht45", "bmp280", "sgp41", "pms5003", "s8" };
    for (int i = 0; i < SENSOR_ID_MAX; ++i) {
        cJSON *entry = cJSON_CreateObject();
        cJSON_AddNumberToObject(entry, "ms", (double)ms[i]);
        cJSON_AddBoolToObject(entry, "from_nvs", from_nvs[i]);
        cJSON_AddItemToObject(cad, names[i], entry);
    }
    respond_json(req, root, 200);
    iaq_prof_toc(IAQ_METRIC_WEB_API_SENSORS, t0);
    return ESP_OK;
}

static bool parse_sensor_name(const char *name, sensor_id_t *out)
{
    if (!name || !out) return false;
    if (strcasecmp(name, "mcu") == 0) { *out = SENSOR_ID_MCU; return true; }
    if (strcasecmp(name, "sht45") == 0) { *out = SENSOR_ID_SHT45; return true; }
    if (strcasecmp(name, "bmp280") == 0) { *out = SENSOR_ID_BMP280; return true; }
    if (strcasecmp(name, "sgp41") == 0) { *out = SENSOR_ID_SGP41; return true; }
    if (strcasecmp(name, "pms5003") == 0) { *out = SENSOR_ID_PMS5003; return true; }
    if (strcasecmp(name, "s8") == 0 || strcasecmp(name, "co2") == 0) { *out = SENSOR_ID_S8; return true; }
    return false;
}

static esp_err_t api_sensor_action(httpd_req_t *req)
{
    uint64_t t0 = iaq_prof_tic();
    const char *uri = req->uri; // /api/v1/sensor/<id>/<action>
    const char *p = strstr(uri, "/api/v1/sensor/");
    if (!p) { respond_error(req, 404, "BAD_URI", "Bad URI"); iaq_prof_toc(IAQ_METRIC_WEB_API_SENSOR_ACTION, t0); return ESP_OK; }
    p += strlen("/api/v1/sensor/");
    char name[16] = {0};
    int i = 0; while (*p && *p != '/' && i < (int)sizeof(name)-1) name[i++] = *p++;
    name[i] = '\0';
    if (*p == '/') p++; // action starts
    char action[16] = {0}; i = 0; while (*p && *p != '/' && i < (int)sizeof(action)-1) action[i++] = *p++;
    action[i] = '\0';

    sensor_id_t id;
    if (!parse_sensor_name(name, &id)) { respond_error(req, 400, "UNKNOWN_SENSOR", "Unknown sensor id"); iaq_prof_toc(IAQ_METRIC_WEB_API_SENSOR_ACTION, t0); return ESP_OK; }

    esp_err_t r = ESP_OK;
    if (strcasecmp(action, "read") == 0) {
        r = sensor_coordinator_force_read_sync(id, 3000);
    } else if (strcasecmp(action, "reset") == 0) {
        r = sensor_coordinator_reset(id);
    } else if (strcasecmp(action, "enable") == 0) {
        r = sensor_coordinator_enable(id);
    } else if (strcasecmp(action, "disable") == 0) {
        r = sensor_coordinator_disable(id);
    } else if (strcasecmp(action, "cadence") == 0) {
        cJSON *body = NULL; if (!read_req_json(req, &body)) { respond_error(req, 400, "INVALID_JSON", "Failed to parse JSON body"); iaq_prof_toc(IAQ_METRIC_WEB_API_SENSOR_ACTION, t0); return ESP_OK; }
        cJSON *jms = cJSON_GetObjectItem(body, "ms");
        if (!cJSON_IsNumber(jms)) { cJSON_Delete(body); respond_error(req, 400, "INVALID_MS", "'ms' must be a number"); iaq_prof_toc(IAQ_METRIC_WEB_API_SENSOR_ACTION, t0); return ESP_OK; }
        int ms = (int)jms->valuedouble;
        if (ms < 0) ms = 0;
        if (ms > CONFIG_IAQ_WEB_PORTAL_CADENCE_MAX_MS) ms = CONFIG_IAQ_WEB_PORTAL_CADENCE_MAX_MS;
        r = sensor_coordinator_set_cadence(id, (uint32_t)ms);
        cJSON_Delete(body);
    } else {
        respond_error(req, 400, "UNKNOWN_ACTION", "Unknown sensor action");
        iaq_prof_toc(IAQ_METRIC_WEB_API_SENSOR_ACTION, t0);
        return ESP_OK;
    }

    if (r != ESP_OK) { respond_error(req, 500, esp_err_to_name(r), "Sensor operation failed"); iaq_prof_toc(IAQ_METRIC_WEB_API_SENSOR_ACTION, t0); return ESP_OK; }
    cJSON *ok = cJSON_CreateObject(); cJSON_AddStringToObject(ok, "status", "ok");
    respond_json(req, ok, 200);
    /* With 1 Hz periodic health, no need to push immediately */
    iaq_prof_toc(IAQ_METRIC_WEB_API_SENSOR_ACTION, t0);
    return ESP_OK;
}

static const char* authmode_to_str(wifi_auth_mode_t m)
{
    switch (m) {
        case WIFI_AUTH_WEP: return "WEP";
        case WIFI_AUTH_WPA_PSK: return "WPA";
        case WIFI_AUTH_WPA2_PSK: return "WPA2";
        case WIFI_AUTH_WPA_WPA2_PSK: return "WPA/WPA2";
        case WIFI_AUTH_WPA3_PSK: return "WPA3";
        case WIFI_AUTH_WPA2_WPA3_PSK: return "WPA2/WPA3";
        default: return "OPEN";
    }
}

static esp_err_t api_wifi_scan_get(httpd_req_t *req)
{
    uint64_t t0 = iaq_prof_tic();
    uint16_t max_aps = CONFIG_IAQ_WEB_PORTAL_WIFI_SCAN_LIMIT;
    /* Query params: ?limit=&offset= */
    int qlen = httpd_req_get_url_query_len(req);
    if (qlen > 0) {
        char *q = malloc(qlen + 1);
        if (q && httpd_req_get_url_query_str(req, q, qlen + 1) == ESP_OK) {
            char val[16];
            if (httpd_query_key_value(q, "limit", val, sizeof(val)) == ESP_OK) {
                int l = atoi(val);
                if (l >= 5 && l <= 100) max_aps = (uint16_t)l;
            }
            /* offset is parsed but currently unused, kept for API forward compatibility */
        }
        free(q);
    }
    wifi_ap_record_t *aps = calloc(max_aps, sizeof(wifi_ap_record_t));
    if (!aps) { respond_error(req, 500, "OOM", "Out of memory"); iaq_prof_toc(IAQ_METRIC_WEB_API_WIFI_SCAN, t0); return ESP_OK; }
    uint16_t found = 0;
    esp_err_t r = wifi_manager_scan(aps, max_aps, &found);
    if (r != ESP_OK) {
        cJSON *err = cJSON_CreateObject();
        cJSON_AddStringToObject(err, "error", esp_err_to_name(r));
        if (r == ESP_ERR_NOT_SUPPORTED) cJSON_AddStringToObject(err, "note", "scan not supported in AP mode");
        respond_json(req, err, 400);
        free(aps);
        iaq_prof_toc(IAQ_METRIC_WEB_API_WIFI_SCAN, t0);
        return ESP_OK;
    }
    cJSON *root = cJSON_CreateObject();
    cJSON *arr = cJSON_CreateArray();
    for (int i = 0; i < found; ++i) {
        cJSON *a = cJSON_CreateObject();
        cJSON_AddStringToObject(a, "ssid", (const char*)aps[i].ssid);
        cJSON_AddNumberToObject(a, "rssi", aps[i].rssi);
        cJSON_AddNumberToObject(a, "channel", aps[i].primary);
        cJSON_AddStringToObject(a, "auth", authmode_to_str(aps[i].authmode));
        cJSON_AddItemToArray(arr, a);
    }
    cJSON_AddItemToObject(root, "aps", arr);
    respond_json(req, root, 200);
    free(aps);
    iaq_prof_toc(IAQ_METRIC_WEB_API_WIFI_SCAN, t0);
    return ESP_OK;
}

static esp_err_t api_wifi_post(httpd_req_t *req)
{
    uint64_t t0 = iaq_prof_tic();
    cJSON *root = NULL;
    if (!read_req_json(req, &root)) { respond_error(req, 400, "INVALID_JSON", "Failed to parse JSON body"); iaq_prof_toc(IAQ_METRIC_WEB_API_WIFI_POST, t0); return ESP_OK; }
    const cJSON *jssid = cJSON_GetObjectItem(root, "ssid");
    const cJSON *jpass = cJSON_GetObjectItem(root, "password");
    const cJSON *jrestart = cJSON_GetObjectItem(root, "restart");
    if (!cJSON_IsString(jssid) || !cJSON_IsString(jpass)) { cJSON_Delete(root); respond_error(req, 400, "SSID_OR_PASSWORD", "Missing or invalid ssid/password"); iaq_prof_toc(IAQ_METRIC_WEB_API_WIFI_POST, t0); return ESP_OK; }
    if (strlen(jssid->valuestring) == 0 || strlen(jssid->valuestring) > 32) { cJSON_Delete(root); respond_error(req, 400, "SSID_LEN", "SSID length must be 1..32"); iaq_prof_toc(IAQ_METRIC_WEB_API_WIFI_POST, t0); return ESP_OK; }
    if (strlen(jpass->valuestring) > 64) { cJSON_Delete(root); respond_error(req, 400, "PASS_LEN", "Password length must be 0..64"); iaq_prof_toc(IAQ_METRIC_WEB_API_WIFI_POST, t0); return ESP_OK; }
    esp_err_t r = wifi_manager_set_credentials(jssid->valuestring, jpass->valuestring);
    if (r != ESP_OK) { cJSON_Delete(root); respond_error(req, 500, esp_err_to_name(r), "Failed to save credentials"); iaq_prof_toc(IAQ_METRIC_WEB_API_WIFI_POST, t0); return ESP_OK; }
    bool restart = cJSON_IsBool(jrestart) ? cJSON_IsTrue(jrestart) : false;
    cJSON_Delete(root);
    if (restart) {
        wifi_manager_stop(); vTaskDelay(pdMS_TO_TICKS(500)); wifi_manager_start();
    }
    cJSON *ok = cJSON_CreateObject(); cJSON_AddStringToObject(ok, "status", "ok");
    respond_json(req, ok, 200);
    iaq_prof_toc(IAQ_METRIC_WEB_API_WIFI_POST, t0);
    return ESP_OK;
}

static esp_err_t api_wifi_restart_post(httpd_req_t *req)
{
    wifi_manager_stop(); vTaskDelay(pdMS_TO_TICKS(500)); wifi_manager_start();
    cJSON *ok = cJSON_CreateObject(); cJSON_AddStringToObject(ok, "status", "restarting");
    respond_json(req, ok, 200);
    return ESP_OK;
}

static esp_err_t api_mqtt_get(httpd_req_t *req)
{
    cJSON *root = cJSON_CreateObject();
    char url[128] = {0};
    mqtt_manager_get_broker_url(url, sizeof(url));
    cJSON_AddStringToObject(root, "broker_url", url);
    cJSON_AddBoolToObject(root, "configured", mqtt_manager_is_configured());
    cJSON_AddBoolToObject(root, "connected", mqtt_manager_is_connected());
    respond_json(req, root, 200);
    return ESP_OK;
}

static esp_err_t api_mqtt_post(httpd_req_t *req)
{
    uint64_t t0 = iaq_prof_tic();
    cJSON *root = NULL;
    if (!read_req_json(req, &root)) { respond_error(req, 400, "INVALID_JSON", "Failed to parse JSON body"); iaq_prof_toc(IAQ_METRIC_WEB_API_MQTT_POST, t0); return ESP_OK; }
    const cJSON *jurl = cJSON_GetObjectItem(root, "broker_url");
    const cJSON *juser = cJSON_GetObjectItem(root, "username");
    const cJSON *jpass = cJSON_GetObjectItem(root, "password");
    const cJSON *jrestart = cJSON_GetObjectItem(root, "restart");
    if (!cJSON_IsString(jurl)) { cJSON_Delete(root); respond_error(req, 400, "BROKER_URL", "Missing broker_url"); iaq_prof_toc(IAQ_METRIC_WEB_API_MQTT_POST, t0); return ESP_OK; }
    const char *user = cJSON_IsString(juser) ? juser->valuestring : NULL;
    const char *pass = cJSON_IsString(jpass) ? jpass->valuestring : NULL;
    esp_err_t r = mqtt_manager_set_broker(jurl->valuestring, user, pass);
    bool restart = cJSON_IsBool(jrestart) ? cJSON_IsTrue(jrestart) : false;
    cJSON_Delete(root);
    if (r != ESP_OK) { respond_error(req, 500, esp_err_to_name(r), "Failed to save MQTT settings"); iaq_prof_toc(IAQ_METRIC_WEB_API_MQTT_POST, t0); return ESP_OK; }
    if (restart) {
        mqtt_manager_stop(); vTaskDelay(pdMS_TO_TICKS(300));
        if (wifi_manager_is_connected()) (void)mqtt_manager_start();
    }
    cJSON *ok = cJSON_CreateObject(); cJSON_AddStringToObject(ok, "status", "ok");
    respond_json(req, ok, 200);
    iaq_prof_toc(IAQ_METRIC_WEB_API_MQTT_POST, t0);
    return ESP_OK;
}

static void restart_deferred(void *arg)
{ (void)arg; vTaskDelay(pdMS_TO_TICKS(200)); esp_restart(); }

static esp_err_t api_device_restart(httpd_req_t *req)
{
    cJSON *ok = cJSON_CreateObject(); cJSON_AddStringToObject(ok, "status", "restarting");
    respond_json(req, ok, 200);
    xTaskCreate(restart_deferred, "reboot", 2048, NULL, 1, NULL);
    return ESP_OK;
}

/* Sensors overview (same structure as health.sensors) */
static esp_err_t api_sensors_get(httpd_req_t *req)
{
    uint64_t t0 = iaq_prof_tic();
    iaq_data_t snap = (iaq_data_t){0}; IAQ_DATA_WITH_LOCK(){ snap = *iaq_data_get(); }
    cJSON *root = iaq_json_build_health(&snap);
    /* Extract just sensors */
    cJSON *sensors = cJSON_DetachItemFromObject(root, "sensors");
    cJSON_Delete(root);
    cJSON *wrap = cJSON_CreateObject();
    cJSON_AddItemToObject(wrap, "sensors", sensors ? sensors : cJSON_CreateObject());
    respond_json(req, wrap, 200);
    iaq_prof_toc(IAQ_METRIC_WEB_API_SENSORS, t0);
    return ESP_OK;
}

/* ===== WS handler ===== */
static esp_err_t ws_handler(httpd_req_t *req)
{
    if (req->method == HTTP_GET) {
        /* Handshake -> add client */
        int sock = httpd_req_to_sockfd(req);
        bool added = ws_clients_add(sock);
        if (!added) {
            ESP_LOGW(TAG, "WS client rejected (capacity reached): %d", sock);
            /* Politely close: send CLOSE then drop session */
            httpd_ws_frame_t closefrm = { 0 };
            closefrm.type = HTTPD_WS_TYPE_CLOSE;
            (void)httpd_ws_send_frame(req, &closefrm);
            if (s_server) httpd_sess_trigger_close(s_server, sock);
            return ESP_OK;
        }
        ESP_LOGI(TAG, "WS client connected: %d", sock);
        /* Push an immediate snapshot targeted to this client so the UI can
         * render without waiting for the periodic timers. */
        iaq_data_t snap = (iaq_data_t){0};
        IAQ_DATA_WITH_LOCK(){ snap = *iaq_data_get(); }
        /* Use async sends to avoid blocking the httpd thread */
        ws_send_json_to_fd(sock, "state", iaq_json_build_state(&snap));
        ws_send_json_to_fd(sock, "metrics", iaq_json_build_metrics(&snap));
        ws_send_json_to_fd(sock, "health", iaq_json_build_health(&snap));
        return ESP_OK;
    }

    httpd_ws_frame_t frame = {0};
    frame.type = HTTPD_WS_TYPE_TEXT;
    /* Get frame len */
    esp_err_t ret = httpd_ws_recv_frame(req, &frame, 0);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "WS recv header failed (fd=%d): %s", httpd_req_to_sockfd(req), esp_err_to_name(ret));
        return ret;
    }

    /* Log header info early for debugging */
    ESP_LOGD(TAG, "WS hdr fd=%d type=%d len=%u", httpd_req_to_sockfd(req), (int)frame.type, (unsigned)frame.len);

    /* Refresh PONG timestamp even for zero-length payloads */
    if (frame.type == HTTPD_WS_TYPE_PONG) {
        int sock = httpd_req_to_sockfd(req);
        if (s_ws_mutex) {
            xSemaphoreTake(s_ws_mutex, portMAX_DELAY);
            for (int i = 0; i < MAX_WS_CLIENTS; ++i) {
                if (s_ws_clients[i].active && s_ws_clients[i].sock == sock) {
                    s_ws_clients[i].last_pong_us = esp_timer_get_time();
                    break;
                }
            }
            xSemaphoreGive(s_ws_mutex);
        }
        if (frame.len == 0) {
            /* Fast-path: no payload to read */
            return ESP_OK;
        }
    }
    if (frame.len) {
        uint64_t t0 = iaq_prof_tic();
        uint8_t *buf = (uint8_t*)malloc(frame.len + 1);
        if (!buf) return ESP_ERR_NO_MEM;
        frame.payload = buf;
        ret = httpd_ws_recv_frame(req, &frame, frame.len);
        if (ret == ESP_OK) {
            buf[frame.len] = 0;
            if (frame.type == HTTPD_WS_TYPE_PONG) {
                ESP_LOGD(TAG, "WS PONG fd=%d (%u bytes)", httpd_req_to_sockfd(req), (unsigned)frame.len);
            } else if (frame.type == HTTPD_WS_TYPE_PING) {
                ESP_LOGD(TAG, "WS PING from fd=%d (%u bytes)", httpd_req_to_sockfd(req), (unsigned)frame.len);
                /* Reply with PONG mirroring payload (RFC6455 5.5.2/5.5.3) */
                httpd_ws_frame_t pong = { 0 };
                pong.type = HTTPD_WS_TYPE_PONG;
                pong.payload = frame.payload;
                pong.len = frame.len;
                (void)httpd_ws_send_frame(req, &pong);
            } else {
                ESP_LOGD(TAG, "WS RX fd=%d (%u): %s", httpd_req_to_sockfd(req), (unsigned)frame.len, (char*)buf);
                /* Future: parse commands. For now ignore. */
            }
        } else {
            ESP_LOGW(TAG, "WS recv payload failed (fd=%d, len=%u): %s", httpd_req_to_sockfd(req), (unsigned)frame.len, esp_err_to_name(ret));
        }
        free(buf);
        iaq_prof_toc(IAQ_METRIC_WEB_WS_RX, t0);
    }
    if (frame.type == HTTPD_WS_TYPE_CLOSE) {
        int sock = httpd_req_to_sockfd(req);
        ws_clients_remove(sock);
        ESP_LOGI(TAG, "WS client closed: %d", sock);
        /* Echo a CLOSE back to complete handshake (no body) */
        httpd_ws_frame_t closefrm = { 0 };
        closefrm.type = HTTPD_WS_TYPE_CLOSE;
        (void)httpd_ws_send_frame(req, &closefrm);
    }
    return ret;
}

/* No event-driven health push needed at 1 Hz */

static void iaq_evt_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg; (void)data;
    if (base == WIFI_EVENT) {
        if (id == WIFI_EVENT_AP_START) {
            /* Only enable captive portal when running AP-only (not AP+STA) */
            wifi_mode_t mode = wifi_manager_get_mode();
            if (mode == WIFI_MODE_AP) {
                ESP_LOGI(TAG, "AP-only: enabling captive portal DNS + DHCP URI");
                dhcp_set_captiveportal_uri();
                if (!s_dns) {
                    dns_server_config_t cfg = { .queried_name = "*", .netif_key = "WIFI_AP_DEF" };
                    s_dns = dns_server_start(&cfg);
                }
            } else {
                ESP_LOGI(TAG, "AP started in %s mode; captive DNS disabled", mode == WIFI_MODE_APSTA ? "APSTA" : "other");
            }
        } else if (id == WIFI_EVENT_AP_STOP) {
            ESP_LOGI(TAG, "AP stopped: disabling captive portal DNS");
            if (s_dns) { dns_server_stop(s_dns); s_dns = NULL; }
        }
    }
    if (base == IAQ_EVENT) {
        /* Switch HTTP <-> HTTPS based on STA connectivity only. */
        if (id == IAQ_EVENT_WIFI_CONNECTED || id == IAQ_EVENT_WIFI_DISCONNECTED) {
            bool want_https = web_portal_should_use_https();
            if (s_server && s_server_is_https != want_https) {
                ESP_LOGI(TAG, "Restarting web server for protocol change (%s -> %s)",
                         s_server_is_https ? "HTTPS" : "HTTP",
                         want_https ? "HTTPS" : "HTTP");
                if (!s_portal_restart_pending) {
                    s_portal_restart_pending = true;
                    xTaskCreate(web_portal_restart_task, "wp_restart", 4096, NULL, 4, NULL);
                }
            }
        }
    }
    /* With 1 Hz health periodic, no extra pushes needed */
}

/* ===== Public API ===== */
static void web_portal_restart_task(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(10));
    web_portal_stop();
    vTaskDelay(pdMS_TO_TICKS(10));
    (void)web_portal_start();
    s_portal_restart_pending = false;
    vTaskDelete(NULL);
}

esp_err_t web_portal_init(iaq_system_context_t *ctx)
{
    if (!ctx) return ESP_ERR_INVALID_ARG;
    s_ctx = ctx;

    /* Mount LittleFS (partition label 'www') */
    esp_vfs_littlefs_conf_t conf = {
        .base_path = WEB_MOUNT_POINT,
        .partition_label = "www",
        .format_if_mount_failed = true,
        .dont_mount = false,
    };
    esp_err_t r = esp_vfs_littlefs_register(&conf);
    if (r != ESP_OK) {
        ESP_LOGW(TAG, "LittleFS mount failed: %s (portal will serve API only)", esp_err_to_name(r));
    } else {
        size_t total=0, used=0; if (esp_littlefs_info(conf.partition_label, &total, &used) == ESP_OK) {
            ESP_LOGI(TAG, "LittleFS mounted at %s (%u/%u bytes)", WEB_MOUNT_POINT, (unsigned)used, (unsigned)total);
        }
    }

    ws_clients_init();

    /* Timers */
    const esp_timer_create_args_t t_state = { .callback = &ws_state_timer_cb, .name = "ws_state" };
    const esp_timer_create_args_t t_metrics = { .callback = &ws_metrics_timer_cb, .name = "ws_metrics" };
    const esp_timer_create_args_t t_health = { .callback = &ws_health_timer_cb, .name = "ws_health" };
    ESP_ERROR_CHECK(esp_timer_create(&t_state, &s_ws_state_timer));
    ESP_ERROR_CHECK(esp_timer_create(&t_metrics, &s_ws_metrics_timer));
    ESP_ERROR_CHECK(esp_timer_create(&t_health, &s_ws_health_timer));

    /* React to connectivity events for snappier updates */
    ESP_ERROR_CHECK(esp_event_handler_register(IAQ_EVENT, IAQ_EVENT_WIFI_CONNECTED, &iaq_evt_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IAQ_EVENT, IAQ_EVENT_WIFI_DISCONNECTED, &iaq_evt_handler, NULL));

    /* Start/stop DNS and set DHCP captive portal URI when AP starts/stops */
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_AP_START, &iaq_evt_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_AP_STOP, &iaq_evt_handler, NULL));

    return ESP_OK;
}

esp_err_t web_portal_start(void)
{
    if (s_server) return ESP_OK;
    /* Decide HTTP vs HTTPS at runtime to improve captive-portal UX in AP-only */
    bool use_https = web_portal_should_use_https();

    if (use_https) {
        httpd_ssl_config_t scfg = HTTPD_SSL_CONFIG_DEFAULT();
        httpd_config_t dcfg = HTTPD_DEFAULT_CONFIG();
        scfg.httpd = dcfg;
        scfg.httpd.uri_match_fn = httpd_uri_match_wildcard;
        /* Default LRU purge behavior */
        scfg.httpd.lru_purge_enable = true;
        scfg.httpd.max_uri_handlers = 20;
        /* Moderate simultaneous handshake pressure */
        scfg.httpd.backlog_conn = 3;
        /* Cap HTTPD sockets so other services (MQTT/SNTP/DNS) keep room */
        scfg.httpd.max_open_sockets = MAX_WS_CLIENTS + 4; /* WS clients + few HTTP fetches */
        scfg.httpd.stack_size = TASK_STACK_WEB_SERVER;
        scfg.httpd.core_id = TASK_CORE_WEB_SERVER;
        if (scfg.httpd.stack_size < 6144) scfg.httpd.stack_size = 6144;
        /* Try to load cert/key from LittleFS, fallback to built-in dev cert */
        extern const unsigned char servercert_pem_start[] asm("_binary_servercert_pem_start");
        extern const unsigned char prvtkey_pem_start[] asm("_binary_prvtkey_pem_start");
        extern const unsigned char servercert_pem_end[] asm("_binary_servercert_pem_end");
        extern const unsigned char prvtkey_pem_end[] asm("_binary_prvtkey_pem_end");

        /* Attempt reading from /www/cert.pem and /www/key.pem */
        char *file_cert = NULL, *file_key = NULL;
        size_t file_cert_len = 0, file_key_len = 0;
        {
            FILE *cf = fopen(WEB_MOUNT_POINT "/cert.pem", "rb");
            if (cf) {
                fseek(cf, 0, SEEK_END); long l = ftell(cf); fseek(cf, 0, SEEK_SET);
                if (l > 0 && l < 40960) {
                    file_cert = malloc((size_t)l + 1);
                    if (file_cert) { fread(file_cert, 1, (size_t)l, cf); file_cert[(size_t)l] = '\0'; file_cert_len = (size_t)l + 1; }
                }
                fclose(cf);
            }
            FILE *kf = fopen(WEB_MOUNT_POINT "/key.pem", "rb");
            if (kf) {
                fseek(kf, 0, SEEK_END); long l = ftell(kf); fseek(kf, 0, SEEK_SET);
                if (l > 0 && l < 40960) {
                    file_key = malloc((size_t)l + 1);
                    if (file_key) { fread(file_key, 1, (size_t)l, kf); file_key[(size_t)l] = '\0'; file_key_len = (size_t)l + 1; }
                }
                fclose(kf);
            }
        }

        const unsigned char *cert_ptr = NULL, *key_ptr = NULL;
        size_t cert_len = 0, key_len = 0;
        if (file_cert && file_key) {
            cert_ptr = (const unsigned char *)file_cert; cert_len = file_cert_len;
            key_ptr  = (const unsigned char *)file_key;  key_len  = file_key_len;
            ESP_LOGI(TAG, "HTTPS: using cert/key from LittleFS (%u/%u bytes)", (unsigned)cert_len, (unsigned)key_len);
        } else {
            cert_ptr = (const unsigned char *)servercert_pem_start;
            cert_len = (size_t)(servercert_pem_end - servercert_pem_start);
            key_ptr  = (const unsigned char *)prvtkey_pem_start;
            key_len  = (size_t)(prvtkey_pem_end - prvtkey_pem_start);
            ESP_LOGW(TAG, "HTTPS: using built-in self-signed development certificate");
        }

        scfg.servercert = cert_ptr;
        scfg.servercert_len = cert_len;
        scfg.prvtkey_pem = key_ptr;
        scfg.prvtkey_len = key_len;

        ESP_LOGD(TAG, "HTTPS httpd cfg: port=%d, recv_to=%d, send_to=%d, backlog=%d, max_socks=%d, max_uris=%d",
                 scfg.httpd.server_port, scfg.httpd.recv_wait_timeout, scfg.httpd.send_wait_timeout,
                 scfg.httpd.backlog_conn, scfg.httpd.max_open_sockets, scfg.httpd.max_uri_handlers);
        esp_err_t r = httpd_ssl_start(&s_server, &scfg);
        if (r != ESP_OK) {
            ESP_LOGE(TAG, "Failed to start HTTPS server: %s", esp_err_to_name(r));
            if (file_cert) free(file_cert);
            if (file_key) free(file_key);
            return r;
        }
        if (file_cert) free(file_cert);
        if (file_key) free(file_key);
        s_server_is_https = true;
        /* Register HTTPD task for stack profiling */
#if defined(INCLUDE_xTaskGetHandle) && (INCLUDE_xTaskGetHandle == 1)
        s_httpd_task_handle = xTaskGetHandle("httpd");
        if (s_httpd_task_handle) {
            iaq_profiler_register_task("httpd", s_httpd_task_handle, scfg.httpd.stack_size);
        }
#endif
    } else {
        httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
        cfg.uri_match_fn = httpd_uri_match_wildcard;
        /* Default LRU purge behavior */
        cfg.lru_purge_enable = true;
        cfg.max_uri_handlers = 20;
        /* Moderate simultaneous pending connects to limit spikes */
        cfg.backlog_conn = 3;
        /* Cap HTTPD sockets so other services (MQTT/SNTP/DNS) keep room */
        cfg.max_open_sockets = MAX_WS_CLIENTS + 4; /* WS clients + few HTTP fetches */
        cfg.stack_size = TASK_STACK_WEB_SERVER;
        cfg.core_id = TASK_CORE_WEB_SERVER;
        if (cfg.stack_size < 6144) cfg.stack_size = 6144;
        ESP_LOGD(TAG, "HTTP httpd cfg: port=%d, recv_to=%d, send_to=%d, backlog=%d, max_socks=%d, max_uris=%d",
                 cfg.server_port, cfg.recv_wait_timeout, cfg.send_wait_timeout,
                 cfg.backlog_conn, cfg.max_open_sockets, cfg.max_uri_handlers);
        esp_err_t r = httpd_start(&s_server, &cfg);
        if (r != ESP_OK) {
            ESP_LOGE(TAG, "Failed to start HTTP server: %s", esp_err_to_name(r));
            return r;
        }
        s_server_is_https = false;
        if (wifi_manager_get_mode() == WIFI_MODE_AP) {
            ESP_LOGI(TAG, "AP-only mode: using HTTP to improve captive portal compatibility");
        }
        /* Register HTTPD task for stack profiling */
#if defined(INCLUDE_xTaskGetHandle) && (INCLUDE_xTaskGetHandle == 1)
        s_httpd_task_handle = xTaskGetHandle("httpd");
        if (s_httpd_task_handle) {
            iaq_profiler_register_task("httpd", s_httpd_task_handle, cfg.stack_size);
        }
#endif
    }

    /* Register API handlers */
    const httpd_uri_t uri_options = { .uri = "/api/*", .method = HTTP_OPTIONS, .handler = api_options_handler, .user_ctx = NULL };
    const httpd_uri_t uri_info = { .uri = "/api/v1/info", .method = HTTP_GET, .handler = api_info_get, .user_ctx = NULL };
    const httpd_uri_t uri_state = { .uri = "/api/v1/state", .method = HTTP_GET, .handler = api_state_get, .user_ctx = NULL };
    const httpd_uri_t uri_metrics = { .uri = "/api/v1/metrics", .method = HTTP_GET, .handler = api_metrics_get, .user_ctx = NULL };
    const httpd_uri_t uri_health = { .uri = "/api/v1/health", .method = HTTP_GET, .handler = api_health_get, .user_ctx = NULL };
    const httpd_uri_t uri_wifi_get = { .uri = "/api/v1/wifi", .method = HTTP_GET, .handler = api_wifi_get, .user_ctx = NULL };
    const httpd_uri_t uri_wifi_scan = { .uri = "/api/v1/wifi/scan", .method = HTTP_GET, .handler = api_wifi_scan_get, .user_ctx = NULL };
    const httpd_uri_t uri_wifi_post = { .uri = "/api/v1/wifi", .method = HTTP_POST, .handler = api_wifi_post, .user_ctx = NULL };
    const httpd_uri_t uri_wifi_restart = { .uri = "/api/v1/wifi/restart", .method = HTTP_POST, .handler = api_wifi_restart_post, .user_ctx = NULL };
    const httpd_uri_t uri_mqtt_get = { .uri = "/api/v1/mqtt", .method = HTTP_GET, .handler = api_mqtt_get, .user_ctx = NULL };
    const httpd_uri_t uri_mqtt_post = { .uri = "/api/v1/mqtt", .method = HTTP_POST, .handler = api_mqtt_post, .user_ctx = NULL };
    const httpd_uri_t uri_dev_restart = { .uri = "/api/v1/device/restart", .method = HTTP_POST, .handler = api_device_restart, .user_ctx = NULL };
    const httpd_uri_t uri_sensors = { .uri = "/api/v1/sensors", .method = HTTP_GET, .handler = api_sensors_get, .user_ctx = NULL };
    const httpd_uri_t uri_sensors_cadence = { .uri = "/api/v1/sensors/cadence", .method = HTTP_GET, .handler = api_sensors_cadence_get, .user_ctx = NULL };
    const httpd_uri_t uri_sensor_action = { .uri = "/api/v1/sensor/*", .method = HTTP_POST, .handler = api_sensor_action, .user_ctx = NULL };
    const httpd_uri_t uri_ws = {
        .uri = "/ws",
        .method = HTTP_GET,
        .handler = ws_handler,
        .is_websocket = true,
        .handle_ws_control_frames = true  /* We handle PING/PONG/CLOSE in ws_handler */
    };
    const httpd_uri_t uri_static = { .uri = "/*", .method = HTTP_GET, .handler = static_handler, .user_ctx = NULL };

    httpd_register_uri_handler(s_server, &uri_options);
    httpd_register_uri_handler(s_server, &uri_info);
    httpd_register_uri_handler(s_server, &uri_state);
    httpd_register_uri_handler(s_server, &uri_metrics);
    httpd_register_uri_handler(s_server, &uri_health);
    httpd_register_uri_handler(s_server, &uri_wifi_get);
    httpd_register_uri_handler(s_server, &uri_wifi_scan);
    httpd_register_uri_handler(s_server, &uri_wifi_post);
    httpd_register_uri_handler(s_server, &uri_wifi_restart);
    httpd_register_uri_handler(s_server, &uri_mqtt_get);
    httpd_register_uri_handler(s_server, &uri_mqtt_post);
    httpd_register_uri_handler(s_server, &uri_dev_restart);
    httpd_register_uri_handler(s_server, &uri_sensors);
    httpd_register_uri_handler(s_server, &uri_sensors_cadence);
    httpd_register_uri_handler(s_server, &uri_sensor_action);
    httpd_register_uri_handler(s_server, &uri_ws);
    httpd_register_uri_handler(s_server, &uri_static);
    /* 404 redirect for captive portal */
    httpd_register_err_handler(s_server, HTTPD_404_NOT_FOUND, http_404_error_handler);

    /* WS timers will start on first WS client connection */
    ESP_LOGI(TAG, "WS config: ping=%ds, pong_timeout=%ds, max_clients=%d",
             CONFIG_IAQ_WEB_PORTAL_WS_PING_INTERVAL_SEC,
             CONFIG_IAQ_WEB_PORTAL_WS_PONG_TIMEOUT_SEC,
             MAX_WS_CLIENTS);
    
#ifdef CONFIG_IAQ_WEB_PORTAL_DEBUG_LOGS
    /* Turn up verbosity for troubleshooting WS/TLS issues */
    esp_log_level_set("httpd", ESP_LOG_DEBUG);
    esp_log_level_set("httpd_ws", ESP_LOG_DEBUG);
    esp_log_level_set("esp_https_server", ESP_LOG_DEBUG);
    esp_log_level_set("esp-tls-mbedtls", ESP_LOG_DEBUG);
#endif

    ESP_LOGI(TAG, "Web portal started (%s)", s_server_is_https ? "HTTPS" : "HTTP");
    return ESP_OK;
}

esp_err_t web_portal_stop(void)
{
    if (!s_server) return ESP_OK;
    /* Unregister task before stopping server */
    if (s_httpd_task_handle) {
        iaq_profiler_unregister_task(s_httpd_task_handle);
        s_httpd_task_handle = NULL;
    }
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_timer_stop(s_ws_state_timer));
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_timer_stop(s_ws_metrics_timer));
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_timer_stop(s_ws_health_timer));
    if (s_server_is_https) {
        httpd_ssl_stop(s_server);
    } else {
        httpd_stop(s_server);
    }
    s_server = NULL;
    s_server_is_https = false;
    ESP_LOGI(TAG, "Web portal stopped");
    /* Keep LittleFS mounted for potential reuse; callers can reboot or unmount if needed */
    return ESP_OK;
}
/* ===== Captive portal helpers ===== */
static void dhcp_set_captiveportal_uri(void)
{
    esp_netif_t* netif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
    if (!netif) return;
    esp_netif_ip_info_t ip_info; if (esp_netif_get_ip_info(netif, &ip_info) != ESP_OK) return;
    char ip_addr[16]; inet_ntoa_r(ip_info.ip.addr, ip_addr, sizeof(ip_addr));
    char uri[32]; snprintf(uri, sizeof(uri), "http://%s", ip_addr);
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_netif_dhcps_stop(netif));
    (void)esp_netif_dhcps_option(netif, ESP_NETIF_OP_SET, ESP_NETIF_CAPTIVEPORTAL_URI, uri, strlen(uri));
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_netif_dhcps_start(netif));
}

/* 404 redirect to root (captive behaviour) */
static esp_err_t http_404_error_handler(httpd_req_t *req, httpd_err_code_t err)
{
    (void)err;
    if (strncmp(req->uri, "/api/", 5) == 0) {
        respond_error(req, 404, "NOT_FOUND", "API endpoint not found");
        return ESP_OK;
    }
    httpd_resp_set_status(req, "302 Temporary Redirect");
    httpd_resp_set_hdr(req, "Location", "/");
    httpd_resp_send(req, "Redirect", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}
/* Helper: decide if HTTPS should be used based on build flags + runtime mode
 * Policy: AP-only always HTTP; STA or AP+STA use HTTPS (if enabled). */
static bool web_portal_should_use_https(void)
{
#if CONFIG_IAQ_WEB_PORTAL_ENABLE_HTTPS
    wifi_mode_t m = wifi_manager_get_mode();
    if (m == WIFI_MODE_STA || m == WIFI_MODE_APSTA) return true;
    return false; /* WIFI_MODE_AP or WIFI_MODE_NULL => HTTP */
#else
    return false;
#endif
}
