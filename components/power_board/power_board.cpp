#include "power_board.h"

#include "esp_log.h"
#include "sdkconfig.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <mutex>

#include "Mainboard/Mainboard.h"
#include "iaq_config.h"
#include "iaq_data.h"
#include "power_board_internal.h"
#include "pm_guard.h"
#include "iaq_profiler.h"

using namespace PowerFeather;

static const char *TAG = "POWER_BOARD";

#if CONFIG_IAQ_POWERFEATHER_ENABLE

/* Track last-set outputs we cannot read back from the SDK */
static bool s_en = true;
static bool s_v3v_on = true;
static bool s_vsqt_on = true;
static bool s_stat_on = true;
static bool s_charging_on = false;
static uint16_t s_charge_limit_ma = CONFIG_IAQ_POWERFEATHER_CHARGE_LIMIT_MA;
static uint16_t s_alarm_low_v_mv = 0;
static uint16_t s_alarm_high_v_mv = 0;
static uint8_t s_alarm_low_pct = 0;
static uint16_t s_maintain_mv = CONFIG_IAQ_POWERFEATHER_MAINTAIN_VOLTAGE_MV;

static bool s_init_ok = false;
static std::mutex s_lock;
static TaskHandle_t s_poll_task = NULL;
static void power_poll_task(void *arg);

/* Poll task timing constants */
static constexpr uint32_t POLL_BASE_INTERVAL_MS = CONFIG_IAQ_POWERFEATHER_POLL_INTERVAL_MS;
static constexpr uint32_t POLL_MAX_BACKOFF_MS = 30000;

static esp_err_t pf_to_err(Result r)
{
    switch (r) {
    case Result::Ok: return ESP_OK;
    case Result::InvalidArg: return ESP_ERR_INVALID_ARG;
    case Result::InvalidState: return ESP_ERR_INVALID_STATE;
    case Result::Timeout: return ESP_ERR_TIMEOUT;
    case Result::LockFailed: return ESP_ERR_INVALID_STATE;
    case Result::NotReady: return ESP_ERR_INVALID_STATE;
    case Result::Failure:
    default:
        return ESP_FAIL;
    }
}

class PmNoSleepBusGuard {
public:
    PmNoSleepBusGuard() { pm_guard_lock_no_sleep(); pm_guard_lock_bus(); }
    ~PmNoSleepBusGuard() { pm_guard_unlock_bus(); pm_guard_unlock_no_sleep(); }
};

/**
 * Helper to wrap control function calls with init check, mutex, and PM guard.
 * The callable should return a PowerFeather Result.
 */
template <typename Func>
static esp_err_t guarded_call(Func&& fn)
{
    if (!s_init_ok) return ESP_ERR_NOT_SUPPORTED;
    std::lock_guard<std::mutex> guard(s_lock);
    PmNoSleepBusGuard pm;
    return pf_to_err(fn());
}

static Mainboard::BatteryType cfg_battery_type(void)
{
#if CONFIG_IAQ_POWERFEATHER_BATTERY_ICR18650_26H
    return Mainboard::BatteryType::ICR18650_26H;
#elif CONFIG_IAQ_POWERFEATHER_BATTERY_UR18650ZY
    return Mainboard::BatteryType::UR18650ZY;
#else
    return Mainboard::BatteryType::Generic_3V7;
#endif
}

esp_err_t power_board_init(void)
{
    std::lock_guard<std::mutex> guard(s_lock);
    uint16_t capacity = CONFIG_IAQ_POWERFEATHER_BATTERY_MAH;
    Mainboard::BatteryType type = cfg_battery_type();

    PmNoSleepBusGuard pm;

    Result r = Board.init(capacity, type);
    if (r != Result::Ok) {
        ESP_LOGE(TAG, "PowerFeather init failed: %d", static_cast<int>(r));
        s_init_ok = false;
        if (CONFIG_IAQ_POWERFEATHER_FAIL_SOFT) {
            return ESP_ERR_NOT_SUPPORTED;
        }
        return pf_to_err(r);
    }

    /* Apply configured outputs/limits if provided */
    if (CONFIG_IAQ_POWERFEATHER_MAINTAIN_VOLTAGE_MV > 0) {
        Result rr = Board.setSupplyMaintainVoltage(CONFIG_IAQ_POWERFEATHER_MAINTAIN_VOLTAGE_MV);
        if (rr == Result::Ok) {
            s_maintain_mv = CONFIG_IAQ_POWERFEATHER_MAINTAIN_VOLTAGE_MV;
        } else {
            ESP_LOGW(TAG, "Failed to set maintain voltage: %d", static_cast<int>(rr));
            s_maintain_mv = 0;
        }
    }
    if (CONFIG_IAQ_POWERFEATHER_CHARGE_LIMIT_MA > 0) {
        Result rr = Board.setBatteryChargingMaxCurrent(CONFIG_IAQ_POWERFEATHER_CHARGE_LIMIT_MA);
        if (rr == Result::Ok) {
            s_charge_limit_ma = CONFIG_IAQ_POWERFEATHER_CHARGE_LIMIT_MA;
        } else {
            ESP_LOGW(TAG, "Failed to set charge limit: %d", static_cast<int>(rr));
            s_charge_limit_ma = 0;
        }
    }

    /*
     * Set charging state explicitly on init to ensure consistent state.
     * The charger IC preserves register state across soft resets, so we can't
     * rely on SDK defaults.
     */
    {
        bool enable_charging = CONFIG_IAQ_POWERFEATHER_CHARGING_ON_BOOT;
        Result rr = Board.enableBatteryCharging(enable_charging);
        if (rr == Result::Ok) {
            s_charging_on = enable_charging;
            ESP_LOGI(TAG, "Charging %s on init", enable_charging ? "enabled" : "disabled");
        } else {
            ESP_LOGW(TAG, "Failed to set charging state: %d", static_cast<int>(rr));
        }
    }

    s_init_ok = true;
    ESP_LOGI(TAG, "PowerFeather initialized (capacity=%u mAh, type=%d)", capacity, static_cast<int>(type));
    if (s_poll_task == NULL) {
        BaseType_t r = xTaskCreate(power_poll_task, "pf_poll", TASK_STACK_POWER_POLL,
                                   NULL, TASK_PRIORITY_POWER_POLL, &s_poll_task);
        if (r != pdPASS) {
            ESP_LOGE(TAG, "Failed to create PowerFeather poll task");
            s_poll_task = NULL;
        } else {
            iaq_profiler_register_task("pf_poll", s_poll_task, TASK_STACK_POWER_POLL);
        }
    }
    return ESP_OK;
}

bool power_board_is_enabled(void)
{
    return s_init_ok;
}

esp_err_t power_board_get_snapshot(power_board_snapshot_t *out)
{
    if (!out) return ESP_ERR_INVALID_ARG;
    *out = {};
    if (!s_init_ok) return ESP_ERR_NOT_SUPPORTED;

    /*
     * Phase 1: Read SDK values WITHOUT holding our mutex.
     * The SDK's Mainboard class has its own internal mutex that protects I2C operations.
     * This avoids blocking control operations during slow I2C reads (~100ms each).
     */
    PmNoSleepBusGuard pm;

    bool any_ok = false;
    esp_err_t first_err = ESP_OK;

    auto record_err = [&](Result res) {
        if (res == Result::Ok) {
            any_ok = true;
            return;
        }
        if (first_err == ESP_OK && res != Result::NotReady) {
            first_err = pf_to_err(res);
        }
    };

    /* Read helpers to reduce repetition */
    auto read_bool = [&](bool& field, Result (*getter)(bool&)) {
        bool tmp = false;
        Result r = getter(tmp);
        if (r == Result::Ok) field = tmp;
        record_err(r);
    };

    auto read_u16 = [&](uint16_t& field, Result (*getter)(uint16_t&)) {
        uint16_t tmp = 0;
        Result r = getter(tmp);
        if (r == Result::Ok) field = tmp;
        record_err(r);
    };

    auto read_s16 = [&](int16_t& field, Result (*getter)(int16_t&)) {
        int16_t tmp = 0;
        Result r = getter(tmp);
        if (r == Result::Ok) field = tmp;
        record_err(r);
    };

    auto read_u8 = [&](uint8_t& field, Result (*getter)(uint8_t&)) {
        uint8_t tmp = 0;
        Result r = getter(tmp);
        if (r == Result::Ok) field = tmp;
        record_err(r);
    };

    /* Supply readings */
    read_bool(out->supply_good, [](bool& v) { return Board.checkSupplyGood(v); });
    read_u16(out->supply_mv, [](uint16_t& v) { return Board.getSupplyVoltage(v); });
    read_s16(out->supply_ma, [](int16_t& v) { return Board.getSupplyCurrent(v); });

    /* Battery readings */
    read_u16(out->batt_mv, [](uint16_t& v) { return Board.getBatteryVoltage(v); });
    read_s16(out->batt_ma, [](int16_t& v) { return Board.getBatteryCurrent(v); });
    read_u8(out->charge_pct, [](uint8_t& v) { return Board.getBatteryCharge(v); });
    read_u8(out->health_pct, [](uint8_t& v) { return Board.getBatteryHealth(v); });
    read_u16(out->cycles, [](uint16_t& v) { return Board.getBatteryCycles(v); });

    /* Time left (int type) */
    {
        int minutes = 0;
        Result r = Board.getBatteryTimeLeft(minutes);
        if (r == Result::Ok) out->time_left_min = minutes;
        record_err(r);
    }

    /* Temperature (float type) */
    {
        float temp = 0.0f;
        Result r = Board.getBatteryTemperature(temp);
        if (r == Result::Ok) out->batt_temp_c = temp;
        record_err(r);
    }

    /*
     * Phase 2: Briefly lock to copy cached control state.
     * These are write-only registers that we track locally.
     */
    {
        std::lock_guard<std::mutex> guard(s_lock);
        out->en = s_en;
        out->v3v_on = s_v3v_on;
        out->vsqt_on = s_vsqt_on;
        out->stat_on = s_stat_on;
        out->charging_on = s_charging_on;
        out->charge_limit_ma = s_charge_limit_ma;
        out->maintain_mv = s_maintain_mv;
        out->alarm_low_v_mv = s_alarm_low_v_mv;
        out->alarm_high_v_mv = s_alarm_high_v_mv;
        out->alarm_low_pct = s_alarm_low_pct;
    }

    out->updated_at_us = esp_timer_get_time();

    if (!any_ok) {
        return (first_err != ESP_OK) ? first_err : ESP_ERR_INVALID_STATE;
    }
    return ESP_OK;
}

esp_err_t power_board_store_snapshot(const power_board_snapshot_t *snap)
{
    if (!snap) return ESP_ERR_INVALID_ARG;
    IAQ_DATA_WITH_LOCK() {
        iaq_data_t *d = iaq_data_get();
        d->power.available = true;
        d->power.supply_good = snap->supply_good;
        d->power.supply_mv = snap->supply_mv;
        d->power.supply_ma = snap->supply_ma;
        d->power.maintain_mv = snap->maintain_mv;
        d->power.en = snap->en;
        d->power.v3v_on = snap->v3v_on;
        d->power.vsqt_on = snap->vsqt_on;
        d->power.stat_on = snap->stat_on;
        d->power.charging_on = snap->charging_on;
        d->power.charge_limit_ma = snap->charge_limit_ma;
        d->power.batt_mv = snap->batt_mv;
        d->power.batt_ma = snap->batt_ma;
        d->power.charge_pct = snap->charge_pct;
        d->power.health_pct = snap->health_pct;
        d->power.cycles = snap->cycles;
        d->power.time_left_min = snap->time_left_min;
        d->power.batt_temp_c = snap->batt_temp_c;
        d->power.alarm_low_v_mv = snap->alarm_low_v_mv;
        d->power.alarm_high_v_mv = snap->alarm_high_v_mv;
        d->power.alarm_low_pct = snap->alarm_low_pct;
        d->power.updated_us = snap->updated_at_us;
    }
    return ESP_OK;
}

static void power_poll_task(void *arg)
{
    (void)arg;
    power_board_snapshot_t snap = {};
    uint32_t delay_ms = POLL_BASE_INTERVAL_MS;

    while (true) {
        if (s_init_ok) {
            iaq_prof_ctx_t pctx = iaq_prof_start(IAQ_METRIC_POWER_POLL);
            if (power_board_get_snapshot(&snap) == ESP_OK) {
                power_board_store_snapshot(&snap);
                delay_ms = POLL_BASE_INTERVAL_MS; /* Reset on success */
            } else {
                IAQ_DATA_WITH_LOCK() {
                    iaq_data_get()->power.available = false;
                    iaq_data_get()->power.updated_us = esp_timer_get_time();
                }
                /* Exponential backoff on failure, capped at max */
                if (delay_ms < POLL_MAX_BACKOFF_MS) {
                    delay_ms = (delay_ms * 2 > POLL_MAX_BACKOFF_MS) ? POLL_MAX_BACKOFF_MS : delay_ms * 2;
                }
            }
            iaq_prof_end(pctx);
        } else {
            IAQ_DATA_WITH_LOCK() {
                iaq_data_get()->power.available = false;
                iaq_data_get()->power.updated_us = esp_timer_get_time();
            }
            delay_ms = POLL_BASE_INTERVAL_MS;
        }
        vTaskDelay(pdMS_TO_TICKS(delay_ms));
    }
}

esp_err_t power_board_set_en(bool high)
{
    return guarded_call([high] {
        Result r = Board.setEN(high);
        if (r == Result::Ok) s_en = high;
        return r;
    });
}

esp_err_t power_board_enable_3v3(bool enable)
{
    return guarded_call([enable] {
        Result r = Board.enable3V3(enable);
        if (r == Result::Ok) s_v3v_on = enable;
        return r;
    });
}

esp_err_t power_board_enable_vsqt(bool enable)
{
    return guarded_call([enable] {
        Result r = Board.enableVSQT(enable);
        if (r == Result::Ok) s_vsqt_on = enable;
        return r;
    });
}

esp_err_t power_board_enable_stat(bool enable)
{
    return guarded_call([enable] {
        Result r = Board.enableSTAT(enable);
        if (r == Result::Ok) s_stat_on = enable;
        return r;
    });
}

esp_err_t power_board_set_supply_maintain_voltage(uint16_t mv)
{
    return guarded_call([mv] {
        Result r = Board.setSupplyMaintainVoltage(mv);
        if (r == Result::Ok) s_maintain_mv = mv;
        return r;
    });
}

esp_err_t power_board_enable_charging(bool enable)
{
    return guarded_call([enable] {
        Result r = Board.enableBatteryCharging(enable);
        if (r == Result::Ok) s_charging_on = enable;
        return r;
    });
}

esp_err_t power_board_set_charge_limit(uint16_t ma)
{
    return guarded_call([ma] {
        Result r = Board.setBatteryChargingMaxCurrent(ma);
        if (r == Result::Ok) s_charge_limit_ma = ma;
        return r;
    });
}

esp_err_t power_board_set_alarm_low_voltage(uint16_t mv)
{
    return guarded_call([mv] {
        Result r = Board.setBatteryLowVoltageAlarm(mv);
        if (r == Result::Ok) s_alarm_low_v_mv = mv;
        return r;
    });
}

esp_err_t power_board_set_alarm_high_voltage(uint16_t mv)
{
    return guarded_call([mv] {
        Result r = Board.setBatteryHighVoltageAlarm(mv);
        if (r == Result::Ok) s_alarm_high_v_mv = mv;
        return r;
    });
}

esp_err_t power_board_set_alarm_low_charge(uint8_t pct)
{
    return guarded_call([pct] {
        Result r = Board.setBatteryLowChargeAlarm(pct);
        if (r == Result::Ok) s_alarm_low_pct = pct;
        return r;
    });
}

esp_err_t power_board_enter_ship_mode(void)
{
    return guarded_call([] { return Board.enterShipMode(); });
}

esp_err_t power_board_enter_shutdown_mode(void)
{
    return guarded_call([] { return Board.enterShutdownMode(); });
}

esp_err_t power_board_power_cycle(void)
{
    return guarded_call([] { return Board.doPowerCycle(); });
}

#else /* CONFIG_IAQ_POWERFEATHER_ENABLE */

esp_err_t power_board_init(void)
{
    ESP_LOGI(TAG, "PowerFeather support disabled (CONFIG_IAQ_POWERFEATHER_ENABLE=n)");
    return ESP_ERR_NOT_SUPPORTED;
}

bool power_board_is_enabled(void) { return false; }

esp_err_t power_board_get_snapshot(power_board_snapshot_t *out)
{
    if (out) memset(out, 0, sizeof(*out));
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t power_board_store_snapshot(const power_board_snapshot_t *snap) { (void)snap; return ESP_ERR_NOT_SUPPORTED; }
esp_err_t power_board_set_en(bool high) { (void)high; return ESP_ERR_NOT_SUPPORTED; }
esp_err_t power_board_enable_3v3(bool enable) { (void)enable; return ESP_ERR_NOT_SUPPORTED; }
esp_err_t power_board_enable_vsqt(bool enable) { (void)enable; return ESP_ERR_NOT_SUPPORTED; }
esp_err_t power_board_enable_stat(bool enable) { (void)enable; return ESP_ERR_NOT_SUPPORTED; }
esp_err_t power_board_set_supply_maintain_voltage(uint16_t mv) { (void)mv; return ESP_ERR_NOT_SUPPORTED; }
esp_err_t power_board_enable_charging(bool enable) { (void)enable; return ESP_ERR_NOT_SUPPORTED; }
esp_err_t power_board_set_charge_limit(uint16_t ma) { (void)ma; return ESP_ERR_NOT_SUPPORTED; }
esp_err_t power_board_set_alarm_low_voltage(uint16_t mv) { (void)mv; return ESP_ERR_NOT_SUPPORTED; }
esp_err_t power_board_set_alarm_high_voltage(uint16_t mv) { (void)mv; return ESP_ERR_NOT_SUPPORTED; }
esp_err_t power_board_set_alarm_low_charge(uint8_t pct) { (void)pct; return ESP_ERR_NOT_SUPPORTED; }
esp_err_t power_board_enter_ship_mode(void) { return ESP_ERR_NOT_SUPPORTED; }
esp_err_t power_board_enter_shutdown_mode(void) { return ESP_ERR_NOT_SUPPORTED; }
esp_err_t power_board_power_cycle(void) { return ESP_ERR_NOT_SUPPORTED; }

#endif /* CONFIG_IAQ_POWERFEATHER_ENABLE */
