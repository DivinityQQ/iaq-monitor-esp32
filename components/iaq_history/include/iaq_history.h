#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "iaq_data.h"

#define HISTORY_SENTINEL INT16_MIN
#define HISTORY_METRIC_COUNT 13

typedef enum {
    HIST_METRIC_TEMP = 0,
    HIST_METRIC_HUMIDITY,
    HIST_METRIC_CO2,
    HIST_METRIC_PRESSURE,
    HIST_METRIC_PM1,
    HIST_METRIC_PM25,
    HIST_METRIC_PM10,
    HIST_METRIC_VOC,
    HIST_METRIC_NOX,
    HIST_METRIC_MOLD_RISK,
    HIST_METRIC_AQI,
    HIST_METRIC_COMFORT,
    HIST_METRIC_IAQ_SCORE,
} history_metric_id_t;

typedef struct {
    int16_t scale;
    int16_t offset;
} history_metric_scale_t;

esp_err_t iaq_history_init(void);
void iaq_history_append(const iaq_data_t *data);
bool iaq_history_metric_scale(history_metric_id_t metric, history_metric_scale_t *out);
void iaq_history_get_stats(uint32_t *used_bytes, uint32_t *total_bytes);

/* ═══════════════════════════════════════════════════════════════════════════
 * STREAMING API - Zero-allocation binary history export
 * ═══════════════════════════════════════════════════════════════════════════ */

/* Parameters computed once, passed to callbacks */
typedef struct {
    uint32_t resolution_s;    /* Effective resolution after grouping */
    int64_t  end_time;        /* Timestamp of last bucket */
    uint16_t bucket_count;    /* Number of output buckets */
    uint8_t  tier;            /* Selected tier (0-2) */
    uint16_t group_factor;    /* Raw buckets per output bucket */
} history_stream_params_t;

/* Compact wire format for buckets (6 bytes, packed) */
typedef struct __attribute__((packed)) {
    int16_t min;
    int16_t max;
    int16_t avg;
} history_bucket_wire_t;

/* Header callback - invoked once before bucket streaming */
typedef bool (*history_header_cb_t)(
    const history_stream_params_t *params,
    const history_metric_id_t *metrics,
    int metric_count,
    void *user_ctx
);

/* Bucket callback - invoked for each batch of aggregated buckets */
typedef bool (*history_bucket_batch_cb_t)(
    history_metric_id_t metric,
    uint16_t start_bucket,
    const history_bucket_wire_t *buckets,
    uint16_t bucket_count,
    void *user_ctx
);

/**
 * Stream history data via callbacks (zero heap allocation).
 *
 * Computes tier/grouping once, iterates all metrics.
 * Aggregates under lock into a caller-provided scratch buffer, then releases
 * the mutex before invoking callbacks (better throughput, slight inconsistency
 * is acceptable).
 *
 * @param metrics       Array of metric IDs to stream
 * @param metric_count  Number of metrics
 * @param start_s       Start time (unix seconds), 0 = auto
 * @param end_s         End time (unix seconds), 0 = now
 * @param max_points    Maximum output buckets, 0 = tier default
 * @param scratch       Caller-provided batch buffer (history_bucket_wire_t[])
 * @param scratch_len   Number of buckets in scratch buffer
 * @param header_cb     Called once with params before streaming
 * @param bucket_cb     Called for each batch (return false to abort)
 * @param user_ctx      Passed to callbacks
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG on bad input,
 *         ESP_FAIL if callback aborted
 */
esp_err_t iaq_history_stream(
    const history_metric_id_t *metrics,
    int metric_count,
    int64_t start_s,
    int64_t end_s,
    uint16_t max_points,
    history_bucket_wire_t *scratch,
    uint16_t scratch_len,
    history_header_cb_t header_cb,
    history_bucket_batch_cb_t bucket_cb,
    void *user_ctx
);
