/* components/console_commands/include/console_commands.h */
#ifndef CONSOLE_COMMANDS_H
#define CONSOLE_COMMANDS_H

#include "esp_err.h"

/**
 * Initialize and start the console command interface.
 * Registers all commands (wifi, mqtt, sensor, system).
 *
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t console_commands_init(void);

#endif /* CONSOLE_COMMANDS_H */