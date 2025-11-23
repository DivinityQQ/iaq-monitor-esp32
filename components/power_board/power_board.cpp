#include "power_board.h"

#include "esp_log.h"
#include "sdkconfig.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <mutex>

#include "Mainboard/Mainboard.h"
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
#if !CONFIG_IAQ_POWERFEATHER_ENABLE
    ESP_LOGI(TAG, "PowerFeather support disabled (CONFIG_IAQ_POWERFEATHER_ENABLE=n)");
    s_init_ok = false;
    return ESP_ERR_NOT_SUPPORTED;
#else
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
    /* Charging default: SDK defaults to disabled; keep that unless we explicitly enable later */

    s_init_ok = true;
    ESP_LOGI(TAG, "PowerFeather initialized (capacity=%u mAh, type=%d)", capacity, static_cast<int>(type));
    if (s_poll_task == NULL) {
        BaseType_t r = xTaskCreate(power_poll_task, "pf_poll", 3072, NULL, 4, &s_poll_task);
        if (r != pdPASS) {
            ESP_LOGE(TAG, "Failed to create PowerFeather poll task");
            s_poll_task = NULL;
        } else {
            iaq_profiler_register_task("pf_poll", s_poll_task, 3072);
        }
    }
    return ESP_OK;
#endif
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

    std::lock_guard<std::mutex> guard(s_lock);
    PmNoSleepBusGuard pm;

    Result r;
    uint16_t u16 = 0;
    int16_t s16 = 0;
    uint8_t u8 = 0;
    bool b = false;
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

    r = Board.checkSupplyGood(b);
    if (r == Result::Ok) {
        out->supply_good = b;
    }
    record_err(r);

    r = Board.getSupplyVoltage(u16);
    if (r == Result::Ok) {
        out->supply_mv = u16;
    }
    record_err(r);

    r = Board.getSupplyCurrent(s16);
    if (r == Result::Ok) {
        out->supply_ma = s16;
    }
    record_err(r);

    out->maintain_mv = s_maintain_mv;

    out->en = s_en;
    out->v3v_on = s_v3v_on;
    out->vsqt_on = s_vsqt_on;
    out->stat_on = s_stat_on;

    r = Board.getBatteryVoltage(u16);
    if (r == Result::Ok) {
        out->batt_mv = u16;
    }
    record_err(r);

    r = Board.getBatteryCurrent(s16);
    if (r == Result::Ok) {
        out->batt_ma = s16;
    }
    record_err(r);

    r = Board.getBatteryCharge(u8);
    if (r == Result::Ok) {
        out->charge_pct = u8;
    }
    record_err(r);

    r = Board.getBatteryHealth(u8);
    if (r == Result::Ok) {
        out->health_pct = u8;
    }
    record_err(r);

    r = Board.getBatteryCycles(u16);
    if (r == Result::Ok) {
        out->cycles = u16;
    }
    record_err(r);

    int minutes = 0;
    r = Board.getBatteryTimeLeft(minutes);
    if (r == Result::Ok) {
        out->time_left_min = minutes;
    }
    record_err(r);

    float temp = 0.0f;
    r = Board.getBatteryTemperature(temp);
    if (r == Result::Ok) {
        out->batt_temp_c = temp;
    }
    record_err(r);

    out->charging_on = s_charging_on;
    out->charge_limit_ma = s_charge_limit_ma;
    out->alarm_low_v_mv = s_alarm_low_v_mv;
    out->alarm_high_v_mv = s_alarm_high_v_mv;
    out->alarm_low_pct = s_alarm_low_pct;

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
    while (true) {
        if (s_init_ok) {
            iaq_prof_ctx_t pctx = iaq_prof_start(IAQ_METRIC_POWER_POLL);
            if (power_board_get_snapshot(&snap) == ESP_OK) {
                power_board_store_snapshot(&snap);
            } else {
                IAQ_DATA_WITH_LOCK() {
                    iaq_data_get()->power.available = false;
                    iaq_data_get()->power.updated_us = esp_timer_get_time();
                }
            }
            iaq_prof_end(pctx);
        } else {
            IAQ_DATA_WITH_LOCK() {
                iaq_data_get()->power.available = false;
                iaq_data_get()->power.updated_us = esp_timer_get_time();
            }
        }
        vTaskDelay(pdMS_TO_TICKS(CONFIG_IAQ_POWERFEATHER_POLL_INTERVAL_MS));
    }
}

esp_err_t power_board_set_en(bool high)
{
    if (!s_init_ok) return ESP_ERR_NOT_SUPPORTED;
    std::lock_guard<std::mutex> guard(s_lock);
    PmNoSleepBusGuard pm;
    Result r = Board.setEN(high);
    if (r == Result::Ok) s_en = high;
    return pf_to_err(r);
}

esp_err_t power_board_enable_3v3(bool enable)
{
    if (!s_init_ok) return ESP_ERR_NOT_SUPPORTED;
    std::lock_guard<std::mutex> guard(s_lock);
    PmNoSleepBusGuard pm;
    Result r = Board.enable3V3(enable);
    if (r == Result::Ok) s_v3v_on = enable;
    return pf_to_err(r);
}

esp_err_t power_board_enable_vsqt(bool enable)
{
    if (!s_init_ok) return ESP_ERR_NOT_SUPPORTED;
    std::lock_guard<std::mutex> guard(s_lock);
    PmNoSleepBusGuard pm;
    Result r = Board.enableVSQT(enable);
    if (r == Result::Ok) s_vsqt_on = enable;
    return pf_to_err(r);
}

esp_err_t power_board_enable_stat(bool enable)
{
    if (!s_init_ok) return ESP_ERR_NOT_SUPPORTED;
    std::lock_guard<std::mutex> guard(s_lock);
    PmNoSleepBusGuard pm;
    Result r = Board.enableSTAT(enable);
    if (r == Result::Ok) s_stat_on = enable;
    return pf_to_err(r);
}

esp_err_t power_board_set_supply_maintain_voltage(uint16_t mv)
{
    if (!s_init_ok) return ESP_ERR_NOT_SUPPORTED;
    std::lock_guard<std::mutex> guard(s_lock);
    PmNoSleepBusGuard pm;
    Result r = Board.setSupplyMaintainVoltage(mv);
    if (r == Result::Ok) s_maintain_mv = mv;
    return pf_to_err(r);
}

esp_err_t power_board_enable_charging(bool enable)
{
    if (!s_init_ok) return ESP_ERR_NOT_SUPPORTED;
    std::lock_guard<std::mutex> guard(s_lock);
    PmNoSleepBusGuard pm;
    Result r = Board.enableBatteryCharging(enable);
    if (r == Result::Ok) s_charging_on = enable;
    return pf_to_err(r);
}

esp_err_t power_board_set_charge_limit(uint16_t ma)
{
    if (!s_init_ok) return ESP_ERR_NOT_SUPPORTED;
    std::lock_guard<std::mutex> guard(s_lock);
    PmNoSleepBusGuard pm;
    Result r = Board.setBatteryChargingMaxCurrent(ma);
    if (r == Result::Ok) s_charge_limit_ma = ma;
    return pf_to_err(r);
}

esp_err_t power_board_set_alarm_low_voltage(uint16_t mv)
{
    if (!s_init_ok) return ESP_ERR_NOT_SUPPORTED;
    std::lock_guard<std::mutex> guard(s_lock);
    PmNoSleepBusGuard pm;
    Result r = Board.setBatteryLowVoltageAlarm(mv);
    if (r == Result::Ok) s_alarm_low_v_mv = mv;
    return pf_to_err(r);
}

esp_err_t power_board_set_alarm_high_voltage(uint16_t mv)
{
    if (!s_init_ok) return ESP_ERR_NOT_SUPPORTED;
    std::lock_guard<std::mutex> guard(s_lock);
    PmNoSleepBusGuard pm;
    Result r = Board.setBatteryHighVoltageAlarm(mv);
    if (r == Result::Ok) s_alarm_high_v_mv = mv;
    return pf_to_err(r);
}

esp_err_t power_board_set_alarm_low_charge(uint8_t pct)
{
    if (!s_init_ok) return ESP_ERR_NOT_SUPPORTED;
    std::lock_guard<std::mutex> guard(s_lock);
    PmNoSleepBusGuard pm;
    Result r = Board.setBatteryLowChargeAlarm(pct);
    if (r == Result::Ok) s_alarm_low_pct = pct;
    return pf_to_err(r);
}

esp_err_t power_board_enter_ship_mode(void)
{
    if (!s_init_ok) return ESP_ERR_NOT_SUPPORTED;
    std::lock_guard<std::mutex> guard(s_lock);
    PmNoSleepBusGuard pm;
    return pf_to_err(Board.enterShipMode());
}

esp_err_t power_board_enter_shutdown_mode(void)
{
    if (!s_init_ok) return ESP_ERR_NOT_SUPPORTED;
    std::lock_guard<std::mutex> guard(s_lock);
    PmNoSleepBusGuard pm;
    return pf_to_err(Board.enterShutdownMode());
}

esp_err_t power_board_power_cycle(void)
{
    if (!s_init_ok) return ESP_ERR_NOT_SUPPORTED;
    std::lock_guard<std::mutex> guard(s_lock);
    PmNoSleepBusGuard pm;
    return pf_to_err(Board.doPowerCycle());
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
