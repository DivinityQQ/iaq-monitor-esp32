/* components/system_context/system_context.c */
#include "system_context.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "SYS_CTX";

/**
 * Define the IAQ custom event base.
 * This allows application components to post and subscribe to IAQ-specific events
 * on the default event loop.
 */
ESP_EVENT_DEFINE_BASE(IAQ_EVENT);

esp_err_t iaq_system_context_init(iaq_system_context_t *ctx)
{
    if (!ctx) {
        ESP_LOGE(TAG, "Invalid context pointer");
        return ESP_ERR_INVALID_ARG;
    }

    /* Clear the structure */
    memset(ctx, 0, sizeof(iaq_system_context_t));

    /* Create event group for inter-component synchronization */
    ctx->event_group = xEventGroupCreate();
    if (ctx->event_group == NULL) {
        ESP_LOGE(TAG, "Failed to create event group");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "System context initialized");
    return ESP_OK;
}

void iaq_system_context_deinit(iaq_system_context_t *ctx)
{
    if (!ctx) {
        return;
    }

    if (ctx->event_group) {
        vEventGroupDelete(ctx->event_group);
        ctx->event_group = NULL;
    }

    ESP_LOGI(TAG, "System context deinitialized");
}
