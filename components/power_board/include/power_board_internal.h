#ifndef POWER_BOARD_INTERNAL_H
#define POWER_BOARD_INTERNAL_H

#include "esp_err.h"
#include "power_board.h"

/* Called by the poller to push a snapshot into iaq_data */
esp_err_t power_board_store_snapshot(const power_board_snapshot_t *snap);

#endif /* POWER_BOARD_INTERNAL_H */
