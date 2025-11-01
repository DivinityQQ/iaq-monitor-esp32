// Minimal DNS redirect server API (IPv4 A records).
#pragma once

#include <stdbool.h>
#include "esp_netif.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct dns_server_handle *dns_server_handle_t;

typedef struct {
    const char *queried_name;   // Name to match ("*" for any)
    const char *netif_key;      // esp-netif if_key to answer with its IPv4 (e.g., "WIFI_AP_DEF")
} dns_server_config_t;

dns_server_handle_t dns_server_start(const dns_server_config_t *cfg);
void dns_server_stop(dns_server_handle_t h);

#ifdef __cplusplus
}
#endif

