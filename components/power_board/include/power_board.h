#ifndef POWER_BOARD_H
#define POWER_BOARD_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool supply_good;
    uint16_t supply_mv;
    int16_t supply_ma;
    uint16_t maintain_mv;
    bool en;
    bool v3v_on;
    bool vsqt_on;
    bool stat_on;
    bool charging_on;
    uint16_t charge_limit_ma;
    uint16_t batt_mv;
    int16_t batt_ma;
    uint8_t charge_pct;
    uint8_t health_pct;
    uint16_t cycles;
    int time_left_min;
    float batt_temp_c;
    uint16_t alarm_low_v_mv;
    uint16_t alarm_high_v_mv;
    uint8_t alarm_low_pct;
    uint64_t updated_at_us;
} power_board_snapshot_t;

/**
 * Initialize PowerFeather board support using Kconfig defaults.
 * Returns ESP_ERR_NOT_SUPPORTED when the feature is disabled.
 */
esp_err_t power_board_init(void);

/** True if integration is enabled at build-time and init succeeded. */
bool power_board_is_enabled(void);

/** Snapshot of power/battery metrics (fields may be zeroed if unavailable). */
esp_err_t power_board_get_snapshot(power_board_snapshot_t *out);

/* Control functions (stubs for now; implemented when SDK wiring is added) */
esp_err_t power_board_set_en(bool high);
esp_err_t power_board_enable_3v3(bool enable);
esp_err_t power_board_enable_vsqt(bool enable);
esp_err_t power_board_enable_stat(bool enable);
esp_err_t power_board_set_supply_maintain_voltage(uint16_t mv);
esp_err_t power_board_enable_charging(bool enable);
esp_err_t power_board_set_charge_limit(uint16_t ma);
esp_err_t power_board_set_alarm_low_voltage(uint16_t mv);
esp_err_t power_board_set_alarm_high_voltage(uint16_t mv);
esp_err_t power_board_set_alarm_low_charge(uint8_t pct);
esp_err_t power_board_enter_ship_mode(void);
esp_err_t power_board_enter_shutdown_mode(void);
esp_err_t power_board_power_cycle(void);

#ifdef __cplusplus
}
#endif

#endif /* POWER_BOARD_H */
