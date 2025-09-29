/* components/connectivity/include/wifi_manager.h */
#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include "esp_err.h"

esp_err_t wifi_manager_init(void);
esp_err_t wifi_manager_start(void);
esp_err_t wifi_manager_stop(void);
int32_t wifi_manager_get_rssi(void);
bool wifi_manager_is_connected(void);

#endif /* WIFI_MANAGER_H */