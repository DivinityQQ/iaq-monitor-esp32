/* components/ota_manager/ota_manager.c */
#include <string.h>
#include <stdio.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "esp_app_format.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_system.h"
#include "esp_littlefs.h"

#include "ota_manager.h"

static const char *TAG = "OTA_MGR";

#ifndef CONFIG_IAQ_OTA_WWW_PARTITION_LABEL
#define CONFIG_IAQ_OTA_WWW_PARTITION_LABEL "www"
#endif
#ifndef CONFIG_IAQ_OTA_WWW_MOUNT_POINT
#define CONFIG_IAQ_OTA_WWW_MOUNT_POINT "/www"
#endif

typedef struct {
    ota_type_t active_type;
    ota_state_t state;
    size_t total_size;
    size_t received_size;
    ota_progress_cb_t cb;
    esp_ota_handle_t fw_handle;
    bool fw_handle_valid;
    const esp_partition_t *target_partition;
    bool littlefs_unmounted;
    bool header_checked;
    bool pending_verify;
    char last_error[96];
} ota_ctx_t;

static ota_ctx_t s_ctx = {
    .active_type = OTA_TYPE_NONE,
    .state = OTA_STATE_IDLE,
    .fw_handle = 0,
    .fw_handle_valid = false,
};

static SemaphoreHandle_t s_mutex = NULL;

static inline uint8_t calc_progress(size_t received, size_t total)
{
    if (total == 0) return 0;
    if (received >= total) return 100;
    uint64_t pct = ((uint64_t)received * 100ULL) / (uint64_t)total;
    if (pct > 100) pct = 100;
    return (uint8_t)pct;
}

static bool ota_lock(void)
{
    if (!s_mutex) return false;
    return xSemaphoreTake(s_mutex, portMAX_DELAY) == pdTRUE;
}

static void ota_unlock(void)
{
    if (s_mutex) xSemaphoreGive(s_mutex);
}

static void ota_reset_locked(void)
{
    s_ctx.cb = NULL;
    s_ctx.target_partition = NULL;
    s_ctx.fw_handle = 0;
    s_ctx.fw_handle_valid = false;
    s_ctx.littlefs_unmounted = false;
    s_ctx.header_checked = false;
    /* Keep last state/type/bytes for observability unless update was in-flight */
    if (s_ctx.state == OTA_STATE_RECEIVING || s_ctx.state == OTA_STATE_VALIDATING) {
        s_ctx.state = OTA_STATE_IDLE;
        s_ctx.active_type = OTA_TYPE_NONE;
        s_ctx.total_size = 0;
        s_ctx.received_size = 0;
    }
}

static void ota_emit_progress(bool reset_after, const char *err_override)
{
    ota_progress_cb_t cb = NULL;
    ota_type_t type = OTA_TYPE_NONE;
    ota_state_t state = OTA_STATE_IDLE;
    size_t rec = 0, total = 0;
    char errbuf[sizeof(s_ctx.last_error)] = {0};

    if (ota_lock()) {
        if (err_override) {
            strlcpy(s_ctx.last_error, err_override, sizeof(s_ctx.last_error));
        }
        cb = s_ctx.cb;
        type = s_ctx.active_type;
        state = s_ctx.state;
        rec = s_ctx.received_size;
        total = s_ctx.total_size;
        strlcpy(errbuf, s_ctx.last_error, sizeof(errbuf));
        if (reset_after) {
            ota_reset_locked();
        }
        ota_unlock();
    }

    if (cb) {
        cb(type, state, calc_progress(rec, total), rec, total,
           errbuf[0] ? errbuf : NULL);
    }
}

static bool ota_can_start(void)
{
    bool busy = false;
    if (ota_lock()) {
        busy = (s_ctx.state == OTA_STATE_RECEIVING || s_ctx.state == OTA_STATE_VALIDATING);
        ota_unlock();
    }
    return !busy;
}

esp_err_t ota_manager_init(void)
{
    if (!s_mutex) {
        s_mutex = xSemaphoreCreateMutex();
        if (!s_mutex) {
            return ESP_ERR_NO_MEM;
        }
    }

    const esp_partition_t *running = esp_ota_get_running_partition();
    if (!running) {
        ESP_LOGE(TAG, "Failed to get running partition");
        return ESP_FAIL;
    }

    esp_ota_img_states_t st = ESP_OTA_IMG_UNDEFINED;
    if (esp_ota_get_state_partition(running, &st) == ESP_OK &&
        st == ESP_OTA_IMG_PENDING_VERIFY) {
        if (ota_lock()) {
            s_ctx.pending_verify = true;
            ota_unlock();
        }
        ESP_LOGW(TAG, "Running firmware is pending verification");
    } else {
        if (ota_lock()) {
            s_ctx.pending_verify = false;
            ota_unlock();
        }
    }

    return ESP_OK;
}

esp_err_t ota_manager_mark_valid(void)
{
    const esp_partition_t *running = esp_ota_get_running_partition();
    if (!running) return ESP_ERR_NOT_FOUND;

    esp_ota_img_states_t st = ESP_OTA_IMG_UNDEFINED;
    if (esp_ota_get_state_partition(running, &st) != ESP_OK) {
        return ESP_FAIL;
    }
    if (st != ESP_OTA_IMG_PENDING_VERIFY) {
        return ESP_OK; /* already valid */
    }

    esp_err_t r = esp_ota_mark_app_valid_cancel_rollback();
    if (r == ESP_OK) {
        ESP_LOGI(TAG, "Firmware marked valid; rollback cancelled");
        if (ota_lock()) {
            s_ctx.pending_verify = false;
            ota_unlock();
        }
    } else {
        ESP_LOGE(TAG, "Failed to mark app valid: %s", esp_err_to_name(r));
    }
    return r;
}

esp_err_t ota_manager_rollback(void)
{
    if (!esp_ota_check_rollback_is_possible()) {
        ESP_LOGW(TAG, "Rollback not available");
        return ESP_ERR_INVALID_STATE;
    }
    ESP_LOGW(TAG, "Marking app invalid to trigger rollback and reboot");
    return esp_ota_mark_app_invalid_rollback_and_reboot();
}

esp_err_t ota_manager_get_runtime(ota_runtime_info_t *info)
{
    if (!info) return ESP_ERR_INVALID_ARG;
    memset(info, 0, sizeof(*info));
    info->active_type = OTA_TYPE_NONE;
    info->state = OTA_STATE_IDLE;
    info->active_slot = -1;

    const esp_partition_t *running = esp_ota_get_running_partition();
    if (running) {
        if (running->subtype >= ESP_PARTITION_SUBTYPE_APP_OTA_MIN &&
            running->subtype <= ESP_PARTITION_SUBTYPE_APP_OTA_MAX) {
            info->active_slot = running->subtype - ESP_PARTITION_SUBTYPE_APP_OTA_MIN;
        }
        esp_ota_img_states_t st = ESP_OTA_IMG_UNDEFINED;
        if (esp_ota_get_state_partition(running, &st) == ESP_OK &&
            st == ESP_OTA_IMG_PENDING_VERIFY) {
            info->pending_verify = true;
        }
    }

    if (ota_lock()) {
        info->state = s_ctx.state;
        info->active_type = s_ctx.active_type;
        info->received = s_ctx.received_size;
        info->total = s_ctx.total_size;
        strlcpy(info->last_error, s_ctx.last_error, sizeof(info->last_error));
        info->pending_verify = s_ctx.pending_verify || info->pending_verify;
        ota_unlock();
    }

    info->rollback_available = esp_ota_check_rollback_is_possible();
    return ESP_OK;
}

esp_err_t ota_manager_get_version_info(ota_version_info_t *info)
{
    if (!info) return ESP_ERR_INVALID_ARG;
    memset(info, 0, sizeof(*info));

    const esp_partition_t *running = esp_ota_get_running_partition();
    if (!running) return ESP_ERR_NOT_FOUND;

    esp_app_desc_t desc = {0};
    if (esp_ota_get_partition_description(running, &desc) == ESP_OK) {
        strlcpy(info->firmware.version, desc.version, sizeof(info->firmware.version));
        strlcpy(info->firmware.build_date, desc.date, sizeof(info->firmware.build_date));
        strlcpy(info->firmware.build_time, desc.time, sizeof(info->firmware.build_time));
        strlcpy(info->firmware.idf_version, desc.idf_ver, sizeof(info->firmware.idf_version));
    }

    /* Frontend version: best effort read from mounted filesystem */
    strlcpy(info->frontend.version, "-", sizeof(info->frontend.version));
    FILE *vf = fopen(CONFIG_IAQ_OTA_WWW_MOUNT_POINT "/version.txt", "r");
    if (vf) {
        if (fgets(info->frontend.version, sizeof(info->frontend.version), vf)) {
            size_t l = strlen(info->frontend.version);
            if (l && info->frontend.version[l - 1] == '\n') {
                info->frontend.version[l - 1] = '\0';
            }
        }
        fclose(vf);
    }

    (void)ota_manager_get_runtime(&info->ota);
    return ESP_OK;
}

bool ota_manager_is_busy(void)
{
    bool busy = false;
    if (ota_lock()) {
        busy = (s_ctx.state == OTA_STATE_RECEIVING || s_ctx.state == OTA_STATE_VALIDATING);
        ota_unlock();
    }
    return busy;
}

esp_err_t ota_firmware_begin(size_t total_size, ota_progress_cb_t cb)
{
    if (total_size == 0) return ESP_ERR_INVALID_ARG;
    const esp_partition_t *running = esp_ota_get_running_partition();
    if (running) {
        esp_ota_img_states_t st = ESP_OTA_IMG_UNDEFINED;
        if (esp_ota_get_state_partition(running, &st) == ESP_OK &&
            st == ESP_OTA_IMG_PENDING_VERIFY) {
            ESP_LOGW(TAG, "Firmware is pending verification; blocking new OTA to preserve rollback");
            return ESP_ERR_INVALID_STATE;
        }
    }
    if (!ota_can_start()) return ESP_ERR_INVALID_STATE;

    const esp_partition_t *update_partition = esp_ota_get_next_update_partition(NULL);
    if (!update_partition) {
        ESP_LOGE(TAG, "No OTA partition available");
        return ESP_ERR_NOT_FOUND;
    }
    if (total_size > update_partition->size) {
        ESP_LOGW(TAG, "Firmware image too large (%u > %u)", (unsigned)total_size, (unsigned)update_partition->size);
        return ESP_ERR_INVALID_SIZE;
    }

    esp_ota_handle_t handle = 0;
    esp_err_t r = esp_ota_begin(update_partition, total_size, &handle);
    if (r != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_begin failed: %s", esp_err_to_name(r));
        return r;
    }

    if (ota_lock()) {
        s_ctx.active_type = OTA_TYPE_FIRMWARE;
        s_ctx.state = OTA_STATE_RECEIVING;
        s_ctx.total_size = total_size;
        s_ctx.received_size = 0;
        s_ctx.cb = cb;
        s_ctx.target_partition = update_partition;
        s_ctx.fw_handle = handle;
        s_ctx.fw_handle_valid = true;
        s_ctx.header_checked = false;
        s_ctx.last_error[0] = '\0';
        ota_unlock();
    }

    ota_emit_progress(false, NULL);
    ESP_LOGI(TAG, "Firmware OTA begin -> partition %s (size=%u)", update_partition->label, (unsigned)update_partition->size);
    return ESP_OK;
}

esp_err_t ota_firmware_write(const void *data, size_t len)
{
    if (!data || len == 0) return ESP_ERR_INVALID_ARG;
    const esp_partition_t *part = NULL;
    esp_ota_handle_t handle = 0;
    bool handle_valid = false;
    size_t received_before = 0;
    bool do_header_check = false;

    if (!ota_lock()) return ESP_ERR_INVALID_STATE;
    if (s_ctx.active_type != OTA_TYPE_FIRMWARE || s_ctx.state != OTA_STATE_RECEIVING || !s_ctx.fw_handle_valid) {
        ota_unlock();
        return ESP_ERR_INVALID_STATE;
    }
    part = s_ctx.target_partition;
    handle = s_ctx.fw_handle;
    handle_valid = s_ctx.fw_handle_valid;
    received_before = s_ctx.received_size;
    if (!s_ctx.header_checked &&
        (received_before + len) >= (sizeof(esp_image_header_t) + sizeof(esp_app_desc_t))) {
        do_header_check = true;
    }
    ota_unlock();

    if (!part) {
        ota_firmware_abort();
        return ESP_ERR_INVALID_STATE;
    }
    if (received_before + len > part->size) {
        if (ota_lock()) {
            s_ctx.state = OTA_STATE_ERROR;
            ota_unlock();
        }
        if (handle_valid) {
            (void)esp_ota_abort(handle);
            handle_valid = false;
        }
        ota_emit_progress(true, "FW image exceeds partition");
        return ESP_ERR_INVALID_SIZE;
    }

    esp_err_t r = esp_ota_write(handle, data, len);
    if (r != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_write failed at %u bytes: %s", (unsigned)(received_before + len), esp_err_to_name(r));
        if (ota_lock()) {
            s_ctx.state = OTA_STATE_ERROR;
            ota_unlock();
        }
        if (handle_valid) {
            (void)esp_ota_abort(handle);
            handle_valid = false;
        }
        ota_emit_progress(true, "FW write failed");
        return r;
    }

    if (do_header_check) {
        esp_app_desc_t new_desc = {0};
        esp_err_t hr = esp_ota_get_partition_description(part, &new_desc);
        if (hr != ESP_OK || new_desc.magic_word != ESP_APP_DESC_MAGIC_WORD) {
            ESP_LOGE(TAG, "FW header invalid");
            if (ota_lock()) {
                s_ctx.state = OTA_STATE_ERROR;
                ota_unlock();
            }
            if (handle_valid) {
                (void)esp_ota_abort(handle);
                handle_valid = false;
            }
            ota_emit_progress(true, "FW header invalid");
            return ESP_ERR_INVALID_ARG;
        }

        const esp_partition_t *running = esp_ota_get_running_partition();
        esp_app_desc_t running_desc = {0};
        if (running && esp_ota_get_partition_description(running, &running_desc) == ESP_OK) {
            if (strncmp(new_desc.project_name, running_desc.project_name, sizeof(new_desc.project_name)) != 0) {
                ESP_LOGE(TAG, "FW project mismatch: new '%s' vs running '%s'",
                         new_desc.project_name, running_desc.project_name);
                if (ota_lock()) {
                    s_ctx.state = OTA_STATE_ERROR;
                    ota_unlock();
                }
                if (handle_valid) {
                    (void)esp_ota_abort(handle);
                    handle_valid = false;
                }
                ota_emit_progress(true, "FW project mismatch");
                return ESP_ERR_INVALID_VERSION;
            }
        }

        if (ota_lock()) {
            s_ctx.header_checked = true;
            ota_unlock();
        }
    }

    if (ota_lock()) {
        s_ctx.received_size += len;
        ota_unlock();
    }
    ota_emit_progress(false, NULL);
    return ESP_OK;
}

esp_err_t ota_firmware_end(bool reboot)
{
    const esp_partition_t *part = NULL;
    esp_ota_handle_t handle = 0;

    if (!ota_lock()) return ESP_ERR_INVALID_STATE;
    if (s_ctx.active_type != OTA_TYPE_FIRMWARE || s_ctx.state != OTA_STATE_RECEIVING || !s_ctx.fw_handle_valid) {
        ota_unlock();
        return ESP_ERR_INVALID_STATE;
    }
    part = s_ctx.target_partition;
    handle = s_ctx.fw_handle;
    s_ctx.state = OTA_STATE_VALIDATING;
    ota_unlock();

    ota_emit_progress(false, NULL);

    esp_err_t r = esp_ota_end(handle);
    if (r != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_end failed: %s", esp_err_to_name(r));
        if (ota_lock()) {
            s_ctx.state = OTA_STATE_ERROR;
            s_ctx.fw_handle_valid = false;  /* Handle consumed by esp_ota_end */
            ota_unlock();
        }
        ota_emit_progress(true, "FW validation failed");
        return r;
    }

    r = esp_ota_set_boot_partition(part);
    if (r != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set boot partition: %s", esp_err_to_name(r));
        if (ota_lock()) {
            s_ctx.state = OTA_STATE_ERROR;
            ota_unlock();
        }
        ota_emit_progress(true, "FW boot set failed");
        return r;
    }

    if (ota_lock()) {
        s_ctx.state = OTA_STATE_COMPLETE;
        s_ctx.fw_handle_valid = false;
        ota_unlock();
    }
    ota_emit_progress(true, NULL);

    ESP_LOGI(TAG, "Firmware OTA complete. Reboot required to switch to new image.");
    if (reboot) {
        vTaskDelay(pdMS_TO_TICKS(100));
        esp_restart();
    }
    return ESP_OK;
}

esp_err_t ota_firmware_abort(void)
{
    if (!ota_lock()) return ESP_ERR_INVALID_STATE;
    bool handle_valid = s_ctx.fw_handle_valid;
    esp_ota_handle_t handle = s_ctx.fw_handle;
    bool was_fw = (s_ctx.active_type == OTA_TYPE_FIRMWARE);
    ota_unlock();

    if (was_fw && handle_valid) {
        (void)esp_ota_abort(handle);
    }

    if (was_fw) {
        if (ota_lock()) {
            s_ctx.state = OTA_STATE_ERROR;
            ota_unlock();
        }
        ota_emit_progress(true, "FW update aborted");
    }
    return ESP_OK;
}

static esp_err_t frontend_remount(bool format_on_fail)
{
    esp_vfs_littlefs_conf_t conf = {
        .base_path = CONFIG_IAQ_OTA_WWW_MOUNT_POINT,
        .partition_label = CONFIG_IAQ_OTA_WWW_PARTITION_LABEL,
        .format_if_mount_failed = format_on_fail,
        .dont_mount = false,
    };
    esp_err_t r = esp_vfs_littlefs_register(&conf);
    if (r == ESP_ERR_INVALID_STATE) {
        /* Already mounted */
        return ESP_OK;
    }
    if (r != ESP_OK && format_on_fail) {
        ESP_LOGW(TAG, "LittleFS remount failed (%s), formatting...", esp_err_to_name(r));
        esp_err_t fmt = esp_littlefs_format(conf.partition_label);
        if (fmt == ESP_OK) {
            r = esp_vfs_littlefs_register(&conf);
        } else {
            ESP_LOGE(TAG, "LittleFS format failed: %s", esp_err_to_name(fmt));
            return fmt;
        }
    }
    return r;
}

esp_err_t ota_frontend_begin(size_t total_size, ota_progress_cb_t cb)
{
    if (total_size == 0) return ESP_ERR_INVALID_ARG;
    if (!ota_can_start()) return ESP_ERR_INVALID_STATE;

    const esp_partition_t *part = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_LITTLEFS,
        CONFIG_IAQ_OTA_WWW_PARTITION_LABEL);
    if (!part) {
        ESP_LOGE(TAG, "LittleFS partition '%s' not found", CONFIG_IAQ_OTA_WWW_PARTITION_LABEL);
        return ESP_ERR_NOT_FOUND;
    }
    if (total_size > part->size) {
        ESP_LOGW(TAG, "Frontend image too large (%u > %u)", (unsigned)total_size, (unsigned)part->size);
        return ESP_ERR_INVALID_SIZE;
    }

    /* Unmount if currently mounted */
    bool was_mounted = false;
    if (esp_littlefs_info(part->label, NULL, NULL) == ESP_OK) {
        was_mounted = true;
        esp_err_t ur = esp_vfs_littlefs_unregister(part->label);
        if (ur != ESP_OK) {
            ESP_LOGW(TAG, "Failed to unmount LittleFS before update: %s", esp_err_to_name(ur));
        }
    }

    /* Erase partition to clean slate */
    esp_err_t er = esp_partition_erase_range(part, 0, part->size);
    if (er != ESP_OK) {
        ESP_LOGE(TAG, "Erase LittleFS partition failed: %s", esp_err_to_name(er));
        /* Attempt remount to restore usability */
        (void)frontend_remount(true);
        return er;
    }

    if (ota_lock()) {
        s_ctx.active_type = OTA_TYPE_FRONTEND;
        s_ctx.state = OTA_STATE_RECEIVING;
        s_ctx.total_size = total_size;
        s_ctx.received_size = 0;
        s_ctx.cb = cb;
        s_ctx.target_partition = part;
        s_ctx.fw_handle_valid = false;
        s_ctx.littlefs_unmounted = was_mounted;
        s_ctx.last_error[0] = '\0';
        ota_unlock();
    }

    ota_emit_progress(false, NULL);
    ESP_LOGI(TAG, "Frontend OTA begin -> partition %s (size=%u)", part->label, (unsigned)part->size);
    return ESP_OK;
}

esp_err_t ota_frontend_write(const void *data, size_t len)
{
    if (!data || len == 0) return ESP_ERR_INVALID_ARG;
    const esp_partition_t *part = NULL;
    size_t received_before = 0;

    if (!ota_lock()) return ESP_ERR_INVALID_STATE;
    if (s_ctx.active_type != OTA_TYPE_FRONTEND || s_ctx.state != OTA_STATE_RECEIVING) {
        ota_unlock();
        return ESP_ERR_INVALID_STATE;
    }
    part = s_ctx.target_partition;
    received_before = s_ctx.received_size;
    ota_unlock();

    if (!part) {
        ota_frontend_abort();
        return ESP_ERR_INVALID_STATE;
    }
    if (received_before + len > part->size) {
        if (ota_lock()) {
            s_ctx.state = OTA_STATE_ERROR;
            ota_unlock();
        }
        ota_emit_progress(true, "Frontend image exceeds partition");
        return ESP_ERR_INVALID_SIZE;
    }

    esp_err_t r = esp_partition_write(part, received_before, data, len);
    if (r != ESP_OK) {
        ESP_LOGE(TAG, "Partition write failed at %u: %s", (unsigned)(received_before + len), esp_err_to_name(r));
        if (ota_lock()) {
            s_ctx.state = OTA_STATE_ERROR;
            ota_unlock();
        }
        ota_emit_progress(true, "Frontend write failed");
        (void)frontend_remount(true);
        return r;
    }

    if (ota_lock()) {
        s_ctx.received_size += len;
        ota_unlock();
    }
    ota_emit_progress(false, NULL);
    return ESP_OK;
}

esp_err_t ota_frontend_end(void)
{
    const esp_partition_t *part = NULL;

    if (!ota_lock()) return ESP_ERR_INVALID_STATE;
    if (s_ctx.active_type != OTA_TYPE_FRONTEND || s_ctx.state != OTA_STATE_RECEIVING) {
        ota_unlock();
        return ESP_ERR_INVALID_STATE;
    }
    part = s_ctx.target_partition;
    s_ctx.state = OTA_STATE_VALIDATING;
    ota_unlock();

    ota_emit_progress(false, NULL);

    esp_err_t r = frontend_remount(false);
    if (r != ESP_OK) {
        ESP_LOGE(TAG, "Frontend remount failed after OTA: %s", esp_err_to_name(r));
        if (ota_lock()) {
            s_ctx.state = OTA_STATE_ERROR;
            ota_unlock();
        }
        ota_emit_progress(true, "Frontend remount failed");
        return r;
    }

    if (ota_lock()) {
        s_ctx.state = OTA_STATE_COMPLETE;
        ota_unlock();
    }
    ota_emit_progress(true, NULL);

    size_t total = 0, used = 0;
    if (esp_littlefs_info(part->label, &total, &used) == ESP_OK) {
        ESP_LOGI(TAG, "LittleFS remounted (%u/%u bytes used)", (unsigned)used, (unsigned)total);
    }
    ESP_LOGI(TAG, "Frontend OTA complete (no reboot required)");
    return ESP_OK;
}

esp_err_t ota_frontend_abort(void)
{
    bool was_frontend = false;
    if (!ota_lock()) return ESP_ERR_INVALID_STATE;
    was_frontend = (s_ctx.active_type == OTA_TYPE_FRONTEND);
    ota_unlock();

    if (was_frontend) {
        if (ota_lock()) {
            s_ctx.state = OTA_STATE_ERROR;
            ota_unlock();
        }
        ota_emit_progress(true, "Frontend update aborted");
        /* Best-effort remount */
        (void)frontend_remount(true);
    }
    return ESP_OK;
}
