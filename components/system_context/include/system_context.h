/* components/system_context/include/system_context.h */
#ifndef SYSTEM_CONTEXT_H
#define SYSTEM_CONTEXT_H

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_err.h"
#include "esp_event.h"

/**
 * Custom event base for application-level events.
 * Posted to the default event loop.
 */
ESP_EVENT_DECLARE_BASE(IAQ_EVENT);

/**
 * Application event IDs for IAQ_EVENT base.
 */
enum {
    IAQ_EVENT_WIFI_CONNECTED,      /**< WiFi connected and IP acquired */
    IAQ_EVENT_WIFI_DISCONNECTED,   /**< WiFi disconnected */
};

/**
 * System-wide context structure.
 * Contains resources shared across multiple components.
 */
typedef struct {
    EventGroupHandle_t event_group;
} iaq_system_context_t;

/**
 * Initialize the system context.
 * Creates the event group and any other system-wide resources.
 *
 * @param ctx Pointer to system context structure to initialize
 * @return ESP_OK on success, ESP_FAIL if event group creation fails
 */
esp_err_t iaq_system_context_init(iaq_system_context_t *ctx);

/**
 * Deinitialize the system context.
 * Cleans up resources created during init.
 *
 * @param ctx Pointer to system context structure to deinitialize
 */
void iaq_system_context_deinit(iaq_system_context_t *ctx);

#endif /* SYSTEM_CONTEXT_H */
