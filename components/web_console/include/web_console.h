/* components/web_console/include/web_console.h */
#ifndef WEB_CONSOLE_H
#define WEB_CONSOLE_H

#include <stdbool.h>
#include "esp_err.h"
#include "esp_http_server.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Initialize log capture and console primitives (no handler registration). */
esp_err_t web_console_init(void);

/* Stop web console infrastructure and free resources. */
void web_console_stop(void);

/* Query whether the console infrastructure was initialized. */
bool web_console_is_initialized(void);

/* Set server handle used by async senders (log + console). */
void web_console_set_server(httpd_handle_t server);

/* Reset client state after server restart. */
void web_console_reset_clients(void);

/* URI descriptors to be registered by web_portal. */
extern const httpd_uri_t web_console_uri_log;
extern const httpd_uri_t web_console_uri_console;

#ifdef __cplusplus
}
#endif

#endif /* WEB_CONSOLE_H */
