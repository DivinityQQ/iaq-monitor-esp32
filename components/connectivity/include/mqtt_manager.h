/* components/connectivity/include/mqtt_manager.h */
#ifndef MQTT_MANAGER_H
#define MQTT_MANAGER_H

#include "esp_err.h"
#include "iaq_config.h"

esp_err_t mqtt_manager_init(void);
esp_err_t mqtt_manager_start(void);
esp_err_t mqtt_manager_stop(void);
esp_err_t mqtt_publish_status(const system_info_t *info);
esp_err_t mqtt_publish_sensor_data(const sensor_data_t *data);
bool mqtt_manager_is_connected(void);

#endif /* MQTT_MANAGER_H */
