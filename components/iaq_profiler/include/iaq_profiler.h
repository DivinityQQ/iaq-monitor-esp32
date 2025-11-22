/* components/iaq_profiler/include/iaq_profiler.h */
#ifndef IAQ_PROFILER_H
#define IAQ_PROFILER_H

#include <stdint.h>
#include <stdbool.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"

/* Public metric identifiers (extend as needed) */
typedef enum {
    IAQ_METRIC_SENSOR_MCU_READ = 0,
    IAQ_METRIC_SENSOR_SHT45_READ,
    IAQ_METRIC_SENSOR_BMP280_READ,
    IAQ_METRIC_SENSOR_SGP41_READ,
    IAQ_METRIC_SENSOR_PMS5003_READ,
    IAQ_METRIC_SENSOR_S8_READ,
    IAQ_METRIC_SENSOR_PMS5003_RX,  /* Background RX parse time */

    IAQ_METRIC_FUSION_TICK,
    IAQ_METRIC_METRICS_TICK,

    IAQ_METRIC_MQTT_HEALTH,
    IAQ_METRIC_MQTT_STATE,
    IAQ_METRIC_MQTT_METRICS,
    IAQ_METRIC_MQTT_DIAG,

    IAQ_METRIC_DISPLAY_FRAME,

    /* Web portal metrics */
    IAQ_METRIC_WEB_STATIC,
    IAQ_METRIC_WEB_API_STATE,
    IAQ_METRIC_WEB_API_METRICS,
    IAQ_METRIC_WEB_API_HEALTH,
    IAQ_METRIC_WEB_API_WIFI_SCAN,
    IAQ_METRIC_WEB_API_WIFI_POST,
    IAQ_METRIC_WEB_API_MQTT_POST,
    IAQ_METRIC_WEB_API_SENSORS,
    IAQ_METRIC_WEB_API_SENSOR_ACTION,
    IAQ_METRIC_WEB_WS_BROADCAST,
    IAQ_METRIC_WEB_WS_RX,
    IAQ_METRIC_POWER_POLL,

    IAQ_METRIC_MAX
} iaq_metric_id_t;

/* RAII-style profiling context */
typedef struct {
    uint64_t start_us;
    int id;
} iaq_prof_ctx_t;

#ifdef __cplusplus
extern "C" {
#endif

/* Initialize profiler (idempotent). Safe to call even if profiling disabled. */
void iaq_profiler_init(void);

/* Report current status (simple when profiling is disabled, comprehensive when enabled). */
void iaq_status_report(void);

/* Register a task to include in stack HWM reporting. */
void iaq_profiler_register_task(const char *name, TaskHandle_t handle, uint32_t stack_size_bytes);

/* Unregister a task (e.g., when it is stopped/restarted). Safe to call with NULL. */
void iaq_profiler_unregister_task(TaskHandle_t handle);

/* Record a duration for a metric (microseconds). */
void iaq_profiler_record(int metric_id, uint32_t duration_us);

/* Helpers for easy timing at call sites */
static inline iaq_prof_ctx_t iaq_prof_start(int id)
{
#if CONFIG_IAQ_PROFILING
    iaq_prof_ctx_t ctx = { .start_us = esp_timer_get_time(), .id = id };
    return ctx;
#else
    (void)id; iaq_prof_ctx_t ctx = { 0, 0 }; return ctx;
#endif
}

static inline void iaq_prof_end(iaq_prof_ctx_t ctx)
{
#if CONFIG_IAQ_PROFILING
    uint64_t now = esp_timer_get_time();
    if (ctx.id >= 0) {
        iaq_profiler_record(ctx.id, (uint32_t)(now - ctx.start_us));
    }
#else
    (void)ctx;
#endif
}

/* Lowest overhead helpers for very hot paths */
static inline uint64_t iaq_prof_tic(void)
{
#if CONFIG_IAQ_PROFILING
    return esp_timer_get_time();
#else
    return 0;
#endif
}

static inline void iaq_prof_toc(int id, uint64_t t0)
{
#if CONFIG_IAQ_PROFILING
    uint64_t dt = esp_timer_get_time() - t0;
    iaq_profiler_record(id, (uint32_t)dt);
#else
    (void)id; (void)t0;
#endif
}

#ifdef __cplusplus
}
#endif

#endif /* IAQ_PROFILER_H */
