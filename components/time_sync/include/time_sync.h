/* components/time_sync/include/time_sync.h */
#ifndef TIME_SYNC_H
#define TIME_SYNC_H

#include <stdbool.h>
#include "esp_err.h"
#include "system_context.h"

/* Initialize time sync module (registers handlers, sets TZ). */
esp_err_t time_sync_init(iaq_system_context_t *ctx);

/* Start SNTP client (safe to call multiple times). */
esp_err_t time_sync_start(void);

/* True if system time looks valid (post-SNTP). */
bool time_sync_is_set(void);

/* Block until time is set or timeout_ms elapses. timeout_ms < 0 waits forever. */
esp_err_t time_sync_wait_for(int timeout_ms);

#endif /* TIME_SYNC_H */

