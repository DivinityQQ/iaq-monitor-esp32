/* components/web_portal/include/web_portal.h */
#ifndef WEB_PORTAL_H
#define WEB_PORTAL_H

#include "esp_err.h"
#include "system_context.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Initialize portal: mounts LittleFS and prepares HTTP/WS server. */
esp_err_t web_portal_init(iaq_system_context_t *ctx);

/* Start portal HTTP/WS server. */
esp_err_t web_portal_start(void);

/* Stop portal server (filesystem stays mounted). */
esp_err_t web_portal_stop(void);

/* Return true if HTTP/HTTPS server is currently running. */
bool web_portal_is_running(void);

#ifdef __cplusplus
}
#endif

#endif /* WEB_PORTAL_H */
