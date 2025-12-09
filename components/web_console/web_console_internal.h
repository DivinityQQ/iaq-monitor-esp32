/* Internal helpers shared by web_console source files (not installed). */
#ifndef WEB_CONSOLE_INTERNAL_H
#define WEB_CONSOLE_INTERNAL_H

#include <stdbool.h>
#include <stddef.h>
#include "sdkconfig.h"
#include "esp_http_server.h"

#ifndef CONFIG_IAQ_WEB_CONSOLE_ENABLE
#define CONFIG_IAQ_WEB_CONSOLE_ENABLE 0
#endif
#ifndef CONFIG_IAQ_WEB_CONSOLE_TOKEN
#define CONFIG_IAQ_WEB_CONSOLE_TOKEN ""
#endif
#ifndef CONFIG_IAQ_WEB_CONSOLE_LOG_BUFFER_SIZE
#define CONFIG_IAQ_WEB_CONSOLE_LOG_BUFFER_SIZE 8192
#endif
#ifndef CONFIG_IAQ_WEB_CONSOLE_MAX_LOG_CLIENTS
#define CONFIG_IAQ_WEB_CONSOLE_MAX_LOG_CLIENTS 1
#endif
#ifndef CONFIG_IAQ_WEB_CONSOLE_CMD_RATE_LIMIT
#define CONFIG_IAQ_WEB_CONSOLE_CMD_RATE_LIMIT 5
#endif
#ifndef CONFIG_IAQ_WEB_CONSOLE_MAX_CMD_LEN
#define CONFIG_IAQ_WEB_CONSOLE_MAX_CMD_LEN 256
#endif
#ifndef CONFIG_IAQ_WEB_CONSOLE_LOG_LINE_MAX
#define CONFIG_IAQ_WEB_CONSOLE_LOG_LINE_MAX 512
#endif

/* Mutex timeout for non-blocking operations (ms).
 * Prevents hangs if a bug causes mutex to not be released. */
#define WC_MUTEX_TIMEOUT_MS 1000

typedef struct {
    bool valid;
    bool via_subproto;
    char subproto_echo[32];
} web_console_auth_result_t;

/* Auth helpers */
web_console_auth_result_t web_console_check_auth(httpd_req_t *req, char *token_buf, size_t buf_len);

/* Server accessors */
httpd_handle_t web_console_get_server(void);
void web_console_set_server(httpd_handle_t server);

/* Lifecycle hooks for submodules */
esp_err_t web_console_log_init(void);
void web_console_log_stop(void);
esp_err_t web_console_console_init(void);
void web_console_console_stop(void);
void web_console_reset_console_state(void);
void web_console_reset_log_state(void);

#endif /* WEB_CONSOLE_INTERNAL_H */
