/* components/ota_manager/include/ota_manager.h */
#ifndef OTA_MANAGER_H
#define OTA_MANAGER_H

#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    OTA_TYPE_NONE = 0,
    OTA_TYPE_FIRMWARE,
    OTA_TYPE_FRONTEND,
} ota_type_t;

typedef enum {
    OTA_STATE_IDLE = 0,
    OTA_STATE_RECEIVING,
    OTA_STATE_VALIDATING,
    OTA_STATE_COMPLETE,
    OTA_STATE_ERROR,
} ota_state_t;

typedef void (*ota_progress_cb_t)(
    ota_type_t type,
    ota_state_t state,
    uint8_t progress,
    size_t received,
    size_t total,
    const char *error_msg);

typedef struct {
    char version[32];
    char build_date[16];
    char build_time[16];
    char idf_version[32];
} ota_firmware_info_t;

typedef struct {
    char version[32];
} ota_frontend_info_t;

typedef struct {
    ota_state_t state;
    ota_type_t active_type;
    int active_slot;                /* 0/1 for ota_0/ota_1, -1 unknown */
    bool rollback_available;
    bool pending_verify;
    size_t received;
    size_t total;
    char last_error[96];
} ota_runtime_info_t;

typedef struct {
    ota_firmware_info_t firmware;
    ota_frontend_info_t frontend;
    ota_runtime_info_t ota;
} ota_version_info_t;

esp_err_t ota_manager_init(void);
esp_err_t ota_manager_mark_valid(void);
esp_err_t ota_manager_rollback(void);

esp_err_t ota_firmware_begin(size_t total_size, ota_progress_cb_t cb);
esp_err_t ota_firmware_write(const void *data, size_t len);
esp_err_t ota_firmware_end(bool reboot);
esp_err_t ota_firmware_abort(void);

esp_err_t ota_frontend_begin(size_t total_size, ota_progress_cb_t cb);
esp_err_t ota_frontend_write(const void *data, size_t len);
esp_err_t ota_frontend_end(void);
esp_err_t ota_frontend_abort(void);

esp_err_t ota_manager_get_version_info(ota_version_info_t *info);
esp_err_t ota_manager_get_runtime(ota_runtime_info_t *info);
bool ota_manager_is_busy(void);

#ifdef __cplusplus
}
#endif

#endif /* OTA_MANAGER_H */
