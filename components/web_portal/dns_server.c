// Minimal DNS redirect server that responds to A queries with AP IPv4.

#include <string.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_netif.h"
#include "iaq_config.h"
#include "dns_server.h"

#define DNS_PORT 53
#define DNS_MAX_LEN 256

/* DNS record types and classes */
#define DNS_TYPE_A      1   /* A record (IPv4 address) */
#define DNS_CLASS_IN    1   /* Internet class */
#define DNS_TTL_SEC     60  /* TTL for responses */

typedef struct __attribute__((__packed__)) {
    uint16_t id, flags, qd_count, an_count, ns_count, ar_count;
} dns_header_t;

typedef struct __attribute__((__packed__)) {
    uint16_t type, class;
} dns_question_t;

typedef struct __attribute__((__packed__)) {
    uint16_t ptr_offset, type, class;
    uint32_t ttl;
    uint16_t addr_len;
    uint32_t ip_addr;
} dns_answer_t;

struct dns_server_handle {
    volatile bool started;
    TaskHandle_t task;
    char name_pat[64];
    char if_key[16];
    int sock;
};

static const char *TAG = "DNS_SRV";

static char* parse_dns_name(char *raw, char *pkt_end, char *out, size_t out_len)
{
    char *label = raw, *w = out;
    size_t used = 0;
    /* Iterate through labels until null terminator or bounds exceeded */
    while (label < pkt_end && *label) {
        uint8_t len = (uint8_t)*label;
        /* Bounds check: label data must fit within packet */
        if (label + 1 + len > pkt_end) return NULL;
        /* Output buffer check */
        if (used + len + 1 >= out_len) return NULL;
        memcpy(w, label + 1, len);
        w += len;
        *w++ = '.';
        used += len + 1;
        label += len + 1;
    }
    /* Ensure we found a null terminator within bounds */
    if (label >= pkt_end) return NULL;
    if (used) out[used - 1] = '\0';
    else out[0] = '\0';
    return label + 1;
}

static int build_dns_reply(char *req, int req_len, char *resp, int resp_max, uint32_t ip_addr, const char *name_pat)
{
    if (req_len > resp_max) return -1;
    memcpy(resp, req, req_len);
    dns_header_t *hdr = (dns_header_t *)resp;
    hdr->flags = htons(ntohs(hdr->flags) | 0x8000); /* QR=1 (response) */
    int qd = ntohs(hdr->qd_count);
    hdr->an_count = hdr->qd_count;
    char *p = resp + sizeof(dns_header_t);
    char *end = resp + req_len;
    char *ans = end;
    for (int i = 0; i < qd; ++i) {
        char name[128];
        char *after = parse_dns_name(p, end, name, sizeof(name));
        if (!after) return -1;
        if (after + sizeof(dns_question_t) > end) return -1;
        dns_question_t *q = (dns_question_t *)after;
        uint16_t qtype = ntohs(q->type), qclass = ntohs(q->class);
        bool match = (strcmp(name_pat, "*") == 0) || (strcasecmp(name, name_pat) == 0);
        if (match && qtype == DNS_TYPE_A && qclass == DNS_CLASS_IN) {
            dns_answer_t *a = (dns_answer_t *)ans;
            a->ptr_offset = htons(0xC000 | (uint16_t)(p - resp));
            a->type = htons(DNS_TYPE_A);
            a->class = htons(DNS_CLASS_IN);
            a->ttl = htonl(DNS_TTL_SEC);
            a->addr_len = htons(4);
            a->ip_addr = ip_addr;
            ans += sizeof(dns_answer_t);
        }
        p = after + sizeof(dns_question_t);
    }
    return ans - resp;
}

static void dns_task(void *arg)
{
    dns_server_handle_t h = (dns_server_handle_t)arg;
    char rx[DNS_MAX_LEN], tx[DNS_MAX_LEN], addr_str[64];
    int s = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (s < 0) {
        ESP_LOGE(TAG, "socket failed");
        h->started = false;
        h->task = NULL;
        vTaskDelete(NULL);
        return;
    }
    h->sock = s;
    struct sockaddr_in bind_addr = { .sin_family = AF_INET, .sin_port = htons(DNS_PORT), .sin_addr.s_addr = htonl(INADDR_ANY) };
    if (bind(s, (struct sockaddr*)&bind_addr, sizeof(bind_addr)) < 0) {
        ESP_LOGE(TAG, "bind failed");
        close(s);
        h->sock = -1;
        h->started = false;
        h->task = NULL;
        vTaskDelete(NULL);
        return;
    }
    /* Set a recv timeout so we can check h->started periodically */
    struct timeval tv = { .tv_sec = 1, .tv_usec = 0 };
    (void)setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    ESP_LOGI(TAG, "DNS server started on :%d", DNS_PORT);

    // Resolve IP for interface
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey(h->if_key);
    esp_netif_ip_info_t ip; memset(&ip, 0, sizeof(ip));
    if (!netif || esp_netif_get_ip_info(netif, &ip) != ESP_OK) {
        ESP_LOGW(TAG, "netif %s ip unknown; DNS may fail", h->if_key);
    }
    uint32_t ip_addr = ip.ip.addr;

    while (h->started) {
        struct sockaddr_in from; socklen_t sl = sizeof(from);
        int len = recvfrom(s, rx, sizeof(rx), 0, (struct sockaddr*)&from, &sl);
        if (len <= 0) continue;
        inet_ntop(AF_INET, &from.sin_addr, addr_str, sizeof(addr_str));
        int rlen = build_dns_reply(rx, len, tx, sizeof(tx), ip_addr, h->name_pat);
        if (rlen > 0) sendto(s, tx, rlen, 0, (struct sockaddr*)&from, sizeof(from));
    }
    close(s);
    h->sock = -1;
    h->task = NULL;
    vTaskDelete(NULL);
}

dns_server_handle_t dns_server_start(const dns_server_config_t *cfg)
{
    if (!cfg || !cfg->queried_name || !cfg->netif_key) return NULL;
    dns_server_handle_t h = calloc(1, sizeof(*h));
    if (!h) return NULL;
    h->started = true;
    strlcpy(h->name_pat, cfg->queried_name, sizeof(h->name_pat));
    strlcpy(h->if_key, cfg->netif_key, sizeof(h->if_key));
    h->sock = -1;
    BaseType_t ret = xTaskCreatePinnedToCore(dns_task, "dns_server", 4096, h, 4, &h->task,
                                             TASK_CORE_WEB_SERVER);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create DNS task");
        free(h);
        return NULL;
    }
    /* Brief delay to let task initialize; check for early failure */
    vTaskDelay(pdMS_TO_TICKS(50));
    if (!h->started || h->task == NULL) {
        ESP_LOGE(TAG, "DNS task failed during initialization");
        free(h);
        return NULL;
    }
    return h;
}

void dns_server_stop(dns_server_handle_t h)
{
    if (!h) return;
    h->started = false;
    /* Interrupt blocking recv */
    if (h->sock >= 0) {
        shutdown(h->sock, SHUT_RDWR);
        close(h->sock);
        h->sock = -1;
    }
    /* Wait for task to self-delete */
    for (int i = 0; i < 50; ++i) { // up to ~500ms
        if (h->task == NULL) break;
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    free(h);
}
