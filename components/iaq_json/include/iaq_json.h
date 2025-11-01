/* components/iaq_json/include/iaq_json.h */
#ifndef IAQ_JSON_H
#define IAQ_JSON_H

#include "cJSON.h"
#include "iaq_data.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Build JSON objects that mirror MQTT unified topic payloads. */

/* /state payload: fused sensor values + basic metrics */
cJSON* iaq_json_build_state(const iaq_data_t *data);

/* /metrics payload: detailed derived metrics */
cJSON* iaq_json_build_metrics(const iaq_data_t *data);

/* /health payload: system health + per-sensor runtime info */
cJSON* iaq_json_build_health(const iaq_data_t *data);

/* Utility: stringify and free cJSON */
static inline char* iaq_json_to_string_and_delete(cJSON *obj)
{
    if (!obj) return NULL;
    char *s = cJSON_PrintUnformatted(obj);
    cJSON_Delete(obj);
    return s;
}

#ifdef __cplusplus
}
#endif

#endif /* IAQ_JSON_H */

