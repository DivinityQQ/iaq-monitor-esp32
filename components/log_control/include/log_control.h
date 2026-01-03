#ifndef LOG_CONTROL_H
#define LOG_CONTROL_H

#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"
#include "esp_log_level.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t log_control_apply_from_nvs(void);
esp_err_t log_control_set_app_level(esp_log_level_t level, bool persist);
esp_err_t log_control_set_system_level(esp_log_level_t level, bool persist);
esp_err_t log_control_reset_to_defaults(bool persist);
esp_log_level_t log_control_get_app_level(void);
esp_log_level_t log_control_get_system_level(void);
void log_control_get_system_tags(const char ***tags, size_t *count);

#ifdef __cplusplus
}
#endif

#endif /* LOG_CONTROL_H */
