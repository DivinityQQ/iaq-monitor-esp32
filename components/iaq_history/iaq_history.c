#include "iaq_history.h"

#include <limits.h>
#include <math.h>
#include <string.h>
#include <time.h>
#include <stdlib.h>
#include "sdkconfig.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "time_sync.h"

#define HISTORY_TIER_COUNT 3

/* Internal bucket structure for aggregation */
typedef struct {
    int16_t min;
    int16_t max;
    int32_t sum;
    uint16_t count;
    uint16_t _pad;
} history_bucket_t;

#define HISTORY_TIER1_RES_S CONFIG_IAQ_HISTORY_TIER1_RES_S
#define HISTORY_TIER2_RES_S CONFIG_IAQ_HISTORY_TIER2_RES_S
#define HISTORY_TIER3_RES_S CONFIG_IAQ_HISTORY_TIER3_RES_S

#define HISTORY_TIER1_CAPACITY (CONFIG_IAQ_HISTORY_TIER1_WINDOW_S / HISTORY_TIER1_RES_S)
#define HISTORY_TIER2_CAPACITY (CONFIG_IAQ_HISTORY_TIER2_WINDOW_S / HISTORY_TIER2_RES_S)
#define HISTORY_TIER3_CAPACITY (CONFIG_IAQ_HISTORY_TIER3_WINDOW_S / HISTORY_TIER3_RES_S)

#define HISTORY_MAX_POINTS HISTORY_TIER3_CAPACITY

typedef struct {
    uint16_t head;
    uint16_t size;
    uint16_t progress;
    int64_t bucket_start_s;
} history_tier_state_t;

typedef struct {
    history_bucket_t *tiers[HISTORY_TIER_COUNT];
} history_metric_store_t;

static const char *TAG = "IAQ_HISTORY";

static const uint16_t s_tier_capacity[HISTORY_TIER_COUNT] = {
    HISTORY_TIER1_CAPACITY,
    HISTORY_TIER2_CAPACITY,
    HISTORY_TIER3_CAPACITY,
};

static const uint32_t s_tier_resolution_s[HISTORY_TIER_COUNT] = {
    HISTORY_TIER1_RES_S,
    HISTORY_TIER2_RES_S,
    HISTORY_TIER3_RES_S,
};

static const uint16_t s_tier_rollup_ratio[HISTORY_TIER_COUNT] = {
    0,
    HISTORY_TIER2_RES_S / HISTORY_TIER1_RES_S,
    HISTORY_TIER3_RES_S / HISTORY_TIER2_RES_S,
};

static const history_metric_scale_t s_metric_scale[HISTORY_METRIC_COUNT] = {
    [HIST_METRIC_TEMP] = { .scale = 100, .offset = 4000 },
    [HIST_METRIC_HUMIDITY] = { .scale = 10, .offset = 0 },
    [HIST_METRIC_CO2] = { .scale = 1, .offset = 0 },
    [HIST_METRIC_PRESSURE] = { .scale = 10, .offset = 0 },
    [HIST_METRIC_PM1] = { .scale = 10, .offset = 0 },
    [HIST_METRIC_PM25] = { .scale = 10, .offset = 0 },
    [HIST_METRIC_PM10] = { .scale = 10, .offset = 0 },
    [HIST_METRIC_VOC] = { .scale = 1, .offset = 0 },
    [HIST_METRIC_NOX] = { .scale = 1, .offset = 0 },
    [HIST_METRIC_MOLD_RISK] = { .scale = 1, .offset = 0 },
    [HIST_METRIC_AQI] = { .scale = 1, .offset = 0 },
    [HIST_METRIC_COMFORT] = { .scale = 1, .offset = 0 },
    [HIST_METRIC_IAQ_SCORE] = { .scale = 1, .offset = 0 },
};

static history_metric_store_t s_metrics[HISTORY_METRIC_COUNT];
static history_tier_state_t s_tier_state[HISTORY_TIER_COUNT];
static SemaphoreHandle_t s_history_mutex = NULL;
static bool s_initialized = false;
static uint32_t s_total_bytes = 0;

static int64_t align_time(int64_t now_s, uint32_t resolution_s)
{
    if (resolution_s == 0) return now_s;
    return now_s - (now_s % resolution_s);
}

static void bucket_reset(history_bucket_t *bucket)
{
    bucket->min = INT16_MAX;
    bucket->max = INT16_MIN;
    bucket->sum = 0;
    bucket->count = 0;
    bucket->_pad = 0;
}

static void bucket_add_value(history_bucket_t *bucket, int16_t value)
{
    if (value == HISTORY_SENTINEL) return;
    if (bucket->count == 0) {
        bucket->min = value;
        bucket->max = value;
    } else {
        if (value < bucket->min) bucket->min = value;
        if (value > bucket->max) bucket->max = value;
    }
    bucket->sum += value;
    bucket->count++;
}

static void bucket_merge(history_bucket_t *dst, const history_bucket_t *src)
{
    if (src->count == 0) return;
    if (dst->count == 0) {
        dst->min = src->min;
        dst->max = src->max;
        dst->sum = src->sum;
        dst->count = src->count;
        return;
    }
    if (src->min < dst->min) dst->min = src->min;
    if (src->max > dst->max) dst->max = src->max;
    dst->sum += src->sum;
    dst->count += src->count;
}

static void reset_tier_bucket(uint8_t tier, uint16_t index)
{
    for (int metric = 0; metric < HISTORY_METRIC_COUNT; metric++) {
        bucket_reset(&s_metrics[metric].tiers[tier][index]);
    }
}

static void reset_history(int64_t now_s)
{
    for (int tier = 0; tier < HISTORY_TIER_COUNT; tier++) {
        s_tier_state[tier].head = 0;
        s_tier_state[tier].size = 0;
        s_tier_state[tier].progress = 0;
        s_tier_state[tier].bucket_start_s = 0;
        reset_tier_bucket(tier, 0);
    }

    s_tier_state[0].bucket_start_s = align_time(now_s, s_tier_resolution_s[0]);
    s_tier_state[0].size = 1;
}

static int16_t quantize_value(float value, const history_metric_scale_t *scale)
{
    if (!isfinite(value)) return HISTORY_SENTINEL;
    int32_t q = (int32_t)lroundf(value * scale->scale) + scale->offset;
    if (q <= INT16_MIN) return INT16_MIN + 1;
    if (q > INT16_MAX) return INT16_MAX;
    return (int16_t)q;
}

static float metric_value_from_data(const iaq_data_t *data, history_metric_id_t metric)
{
    if (!data) return NAN;
    switch (metric) {
        case HIST_METRIC_TEMP:
            if (!data->valid.temp_c || isnan(data->fused.temp_c)) return NAN;
            return data->fused.temp_c;
        case HIST_METRIC_HUMIDITY:
            if (!data->valid.rh_pct || isnan(data->fused.rh_pct)) return NAN;
            return data->fused.rh_pct;
        case HIST_METRIC_CO2:
            if (!data->valid.co2_ppm || isnan(data->fused.co2_ppm)) return NAN;
            return data->fused.co2_ppm;
        case HIST_METRIC_PRESSURE:
            if (!data->valid.pressure_pa || isnan(data->fused.pressure_pa)) return NAN;
            return data->fused.pressure_pa / 100.0f;
        case HIST_METRIC_PM1:
            if (!data->valid.pm1_ugm3 || isnan(data->fused.pm1_ugm3)) return NAN;
            return data->fused.pm1_ugm3;
        case HIST_METRIC_PM25:
            if (!data->valid.pm25_ugm3 || isnan(data->fused.pm25_ugm3)) return NAN;
            return data->fused.pm25_ugm3;
        case HIST_METRIC_PM10:
            if (!data->valid.pm10_ugm3 || isnan(data->fused.pm10_ugm3)) return NAN;
            return data->fused.pm10_ugm3;
        case HIST_METRIC_VOC:
            if (!data->valid.voc_index || data->raw.voc_index == UINT16_MAX) return NAN;
            return (float)data->raw.voc_index;
        case HIST_METRIC_NOX:
            if (!data->valid.nox_index || data->raw.nox_index == UINT16_MAX) return NAN;
            return (float)data->raw.nox_index;
        case HIST_METRIC_MOLD_RISK:
            if (data->metrics.mold_risk_score == UINT8_MAX) return NAN;
            return (float)data->metrics.mold_risk_score;
        case HIST_METRIC_AQI:
            if (data->metrics.aqi_value == UINT16_MAX) return NAN;
            return (float)data->metrics.aqi_value;
        case HIST_METRIC_COMFORT:
            if (data->metrics.comfort_score == UINT8_MAX) return NAN;
            return (float)data->metrics.comfort_score;
        case HIST_METRIC_IAQ_SCORE:
            if (data->metrics.overall_iaq_score == UINT8_MAX) return NAN;
            return (float)data->metrics.overall_iaq_score;
        default:
            return NAN;
    }
}

static void rollup_tier2(int64_t bucket_start_s)
{
    history_tier_state_t *tier3 = &s_tier_state[2];
    if (tier3->progress == 0) {
        tier3->bucket_start_s = bucket_start_s;
        reset_tier_bucket(2, tier3->head);
    }

    for (int metric = 0; metric < HISTORY_METRIC_COUNT; metric++) {
        history_bucket_t *dst = &s_metrics[metric].tiers[2][tier3->head];
        history_bucket_t *src = &s_metrics[metric].tiers[1][s_tier_state[1].head];
        bucket_merge(dst, src);
    }

    tier3->progress++;
    if (tier3->progress >= s_tier_rollup_ratio[2]) {
        tier3->progress = 0;
        tier3->head = (tier3->head + 1) % s_tier_capacity[2];
        if (tier3->size < s_tier_capacity[2]) tier3->size++;
        tier3->bucket_start_s += s_tier_resolution_s[2];
        reset_tier_bucket(2, tier3->head);
    }
}

static void rollup_tier1(int64_t bucket_start_s)
{
    history_tier_state_t *tier2 = &s_tier_state[1];
    if (tier2->progress == 0) {
        tier2->bucket_start_s = bucket_start_s;
        reset_tier_bucket(1, tier2->head);
    }

    for (int metric = 0; metric < HISTORY_METRIC_COUNT; metric++) {
        history_bucket_t *dst = &s_metrics[metric].tiers[1][tier2->head];
        history_bucket_t *src = &s_metrics[metric].tiers[0][s_tier_state[0].head];
        bucket_merge(dst, src);
    }

    tier2->progress++;
    if (tier2->progress >= s_tier_rollup_ratio[1]) {
        rollup_tier2(tier2->bucket_start_s);
        tier2->progress = 0;
        tier2->head = (tier2->head + 1) % s_tier_capacity[1];
        if (tier2->size < s_tier_capacity[1]) tier2->size++;
        tier2->bucket_start_s += s_tier_resolution_s[1];
        reset_tier_bucket(1, tier2->head);
    }
}

static void advance_tier1(int64_t now_s)
{
    history_tier_state_t *tier1 = &s_tier_state[0];
    const uint32_t res = s_tier_resolution_s[0];

    while (now_s >= (tier1->bucket_start_s + res)) {
        rollup_tier1(tier1->bucket_start_s);
        tier1->head = (tier1->head + 1) % s_tier_capacity[0];
        if (tier1->size < s_tier_capacity[0]) tier1->size++;
        tier1->bucket_start_s += res;
        reset_tier_bucket(0, tier1->head);
    }
}

esp_err_t iaq_history_init(void)
{
    if (s_initialized) return ESP_OK;

    s_history_mutex = xSemaphoreCreateMutex();
    if (!s_history_mutex) {
        ESP_LOGE(TAG, "Failed to create history mutex");
        return ESP_ERR_NO_MEM;
    }

    s_total_bytes = 0;
    for (int metric = 0; metric < HISTORY_METRIC_COUNT; metric++) {
        for (int tier = 0; tier < HISTORY_TIER_COUNT; tier++) {
            size_t bytes = s_tier_capacity[tier] * sizeof(history_bucket_t);
            s_metrics[metric].tiers[tier] = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            if (!s_metrics[metric].tiers[tier]) {
                ESP_LOGE(TAG, "PSRAM allocation failed (metric %d tier %d)", metric, tier);
                for (int m = 0; m <= metric; m++) {
                    for (int t = 0; t < HISTORY_TIER_COUNT; t++) {
                        free(s_metrics[m].tiers[t]);
                        s_metrics[m].tiers[t] = NULL;
                    }
                }
                vSemaphoreDelete(s_history_mutex);
                s_history_mutex = NULL;
                return ESP_ERR_NO_MEM;
            }
            memset(s_metrics[metric].tiers[tier], 0, bytes);
            s_total_bytes += bytes;
        }
    }

    reset_history(time(NULL));
    s_initialized = true;
    ESP_LOGI(TAG, "History initialized (%lu bytes)", (unsigned long)s_total_bytes);
    return ESP_OK;
}

void iaq_history_append(const iaq_data_t *data)
{
    if (!s_initialized || !data) return;
    if (!time_sync_is_set()) return;

    int64_t now_s = time(NULL);
    if (now_s <= 0) return;

    if (!s_history_mutex) return;
    xSemaphoreTake(s_history_mutex, portMAX_DELAY);

    if (s_tier_state[0].bucket_start_s == 0 || now_s < s_tier_state[0].bucket_start_s) {
        reset_history(now_s);
    }

    advance_tier1(now_s);

    uint16_t head = s_tier_state[0].head;
    for (int metric = 0; metric < HISTORY_METRIC_COUNT; metric++) {
        float value = metric_value_from_data(data, (history_metric_id_t)metric);
        int16_t q = quantize_value(value, &s_metric_scale[metric]);
        bucket_add_value(&s_metrics[metric].tiers[0][head], q);
    }

    xSemaphoreGive(s_history_mutex);
}

static uint8_t select_tier_for_range(int64_t range_s)
{
    if (range_s <= 3600) return 0;
    if (range_s <= 86400) return 1;
    return 2;
}

bool iaq_history_metric_scale(history_metric_id_t metric, history_metric_scale_t *out)
{
    if (!out || metric < 0 || metric >= HISTORY_METRIC_COUNT) return false;
    *out = s_metric_scale[metric];
    return true;
}

void iaq_history_get_stats(uint32_t *used_bytes, uint32_t *total_bytes)
{
    if (used_bytes) *used_bytes = s_total_bytes;
    if (total_bytes) *total_bytes = s_total_bytes;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * STREAMING API
 * ═══════════════════════════════════════════════════════════════════════════ */

static inline int16_t bucket_avg(const history_bucket_t *b)
{
    if (!b || b->count == 0) return HISTORY_SENTINEL;
    int32_t sum = b->sum;
    int32_t count = b->count;
    if (sum >= 0) return (int16_t)((sum + count / 2) / count);
    return (int16_t)((sum - count / 2) / count);
}

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
    void *user_ctx)
{
    if (!s_initialized || !s_history_mutex) return ESP_ERR_INVALID_STATE;
    if (!metrics || metric_count <= 0 || metric_count > HISTORY_METRIC_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!scratch || scratch_len == 0 || !header_cb || !bucket_cb) {
        return ESP_ERR_INVALID_ARG;
    }

    /* Normalize time range */
    if (end_s <= 0) end_s = time(NULL);
    if (start_s <= 0 || start_s >= end_s) {
        start_s = end_s - 3600;  /* Default 1 hour */
    }
    int64_t range_s = end_s - start_s;

    /* Select tier and capture state under lock */
    uint8_t tier = select_tier_for_range(range_s);
    xSemaphoreTake(s_history_mutex, portMAX_DELAY);
    history_tier_state_t state = s_tier_state[tier];
    uint32_t resolution = s_tier_resolution_s[tier];
    uint16_t capacity = s_tier_capacity[tier];
    int64_t tier_end_time = state.bucket_start_s; /* match iaq_history_query */
    uint16_t oldest = (state.head + capacity - state.size + 1) % capacity;
    xSemaphoreGive(s_history_mutex);

    /* Handle empty tier */
    if (state.size == 0 || state.bucket_start_s == 0) {
        history_stream_params_t params = {
            .resolution_s = resolution,
            .end_time = 0,
            .bucket_count = 0,
            .tier = tier,
            .group_factor = 1,
        };
        return header_cb(&params, metrics, metric_count, user_ctx) ? ESP_OK : ESP_FAIL;
    }

    /* First pass: count buckets in range (time math identical to iaq_history_query) */
    uint16_t raw_count = 0;
    int64_t actual_end_time = 0;
    for (uint16_t i = 0; i < state.size; i++) {
        int64_t t = tier_end_time - (int64_t)(state.size - i - 1) * resolution;
        if (t >= start_s && t <= end_s) {
            raw_count++;
            actual_end_time = t;
        }
    }

    if (raw_count == 0) {
        history_stream_params_t params = {
            .resolution_s = resolution,
            .end_time = end_s,
            .bucket_count = 0,
            .tier = tier,
            .group_factor = 1,
        };
        return header_cb(&params, metrics, metric_count, user_ctx) ? ESP_OK : ESP_FAIL;
    }

    /* Compute grouping */
    uint16_t target = max_points ? max_points : raw_count;
    if (target > raw_count) target = raw_count;
    uint16_t group = (raw_count + target - 1) / target;
    uint16_t bucket_count = (raw_count + group - 1) / group;

    history_stream_params_t params = {
        .resolution_s = resolution * group,
        .end_time = actual_end_time,
        .bucket_count = bucket_count,
        .tier = tier,
        .group_factor = group,
    };

    if (!header_cb(&params, metrics, metric_count, user_ctx)) {
        return ESP_FAIL;
    }

    /* Stream buckets for each metric (aggregate under lock, send unlocked) */
    for (int m = 0; m < metric_count; m++) {
        history_metric_id_t metric = metrics[m];
        if (metric < 0 || metric >= HISTORY_METRIC_COUNT) continue;

        uint16_t out_idx = 0;
        uint16_t group_count = 0;
        history_bucket_t agg = {0};

        uint16_t i = 0;
        while (i < state.size) {
            uint16_t batch_count = 0;

            xSemaphoreTake(s_history_mutex, portMAX_DELAY);
            history_bucket_t *tier_data = s_metrics[metric].tiers[tier];
            for (; i < state.size && batch_count < scratch_len; i++) {
                int64_t t = tier_end_time - (int64_t)(state.size - i - 1) * resolution;
                if (t < start_s || t > end_s) continue;

                if (group_count == 0) bucket_reset(&agg);
                bucket_merge(&agg, &tier_data[(oldest + i) % capacity]);
                group_count++;

                if (group_count >= group) {
                    history_bucket_wire_t *wire = &scratch[batch_count++];
                    if (agg.count == 0) {
                        wire->min = HISTORY_SENTINEL;
                        wire->max = HISTORY_SENTINEL;
                        wire->avg = HISTORY_SENTINEL;
                    } else {
                        wire->min = agg.min;
                        wire->max = agg.max;
                        wire->avg = bucket_avg(&agg);
                    }
                    group_count = 0;
                }
            }
            xSemaphoreGive(s_history_mutex);

            if (batch_count == 0) continue;
            if (!bucket_cb(metric, out_idx, scratch, batch_count, user_ctx)) {
                return ESP_FAIL;
            }
            out_idx += batch_count;
        }

        if (group_count > 0) {
            history_bucket_wire_t wire;
            if (agg.count == 0) {
                wire.min = HISTORY_SENTINEL;
                wire.max = HISTORY_SENTINEL;
                wire.avg = HISTORY_SENTINEL;
            } else {
                wire.min = agg.min;
                wire.max = agg.max;
                wire.avg = bucket_avg(&agg);
            }
            if (!bucket_cb(metric, out_idx, &wire, 1, user_ctx)) {
                return ESP_FAIL;
            }
        }
    }

    return ESP_OK;
}
