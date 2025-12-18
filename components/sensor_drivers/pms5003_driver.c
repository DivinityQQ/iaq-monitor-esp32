/* components/sensor_drivers/pms5003_driver.c */
#include "pms5003_driver.h"
#include "uart_bus.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "sdkconfig.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include "esp_sleep.h"
#include "driver/gpio.h"
#include "iaq_profiler.h"
#include "pm_guard.h"

#ifdef CONFIG_IAQ_SIMULATION
#include "sensor_sim.h"
#endif

static const char *TAG = "PMS5003_DRIVER";

static bool s_initialized = false;
static uart_port_t s_uart_port = (uart_port_t)CONFIG_IAQ_PMS5003_UART_PORT;
static int s_set_gpio = CONFIG_IAQ_PMS5003_SET_GPIO;
static bool s_use_set = false;
static int s_rst_gpio = CONFIG_IAQ_PMS5003_RST_GPIO;
static bool s_use_rst = false;

#ifdef CONFIG_IAQ_PMS5003_BG_READER
static TaskHandle_t s_rx_task = NULL;
static bool parse_pms_frame(const uint8_t *frame, float *pm1, float *pm25, float *pm10);
static QueueHandle_t s_uart_queue = NULL;
static portMUX_TYPE s_pm_lock = portMUX_INITIALIZER_UNLOCKED;
static float s_smoothed_pm1 = NAN, s_smoothed_pm25 = NAN, s_smoothed_pm10 = NAN;
static int64_t s_last_update_us = 0;
/* Signal for RX task to clear its internal parser buffer (e.g., after reset) */
static volatile bool s_rx_clear_requested = false;

static int cmp_f(const void *a, const void *b)
{
    float fa = *(const float *)a, fb = *(const float *)b;
    return (fa > fb) - (fa < fb);
}

static float parse_alpha(void)
{
    const char *s = CONFIG_IAQ_PMS5003_EWMA_ALPHA;
    char *end = NULL;
    float val = strtof(s, &end);
    if (!(val > 0.0f && val < 1.0f)) val = 0.3f;
    return val;
}

static void update_smoothed(float pm1, float pm25, float pm10)
{
    static bool init = false;
    static float alpha = -1.0f;
    if (alpha < 0.0f) alpha = parse_alpha();

    if (!init) {
        s_smoothed_pm1 = pm1;
        s_smoothed_pm25 = pm25;
        s_smoothed_pm10 = pm10;
        init = true;
    } else {
        s_smoothed_pm1  = alpha * pm1  + (1.0f - alpha) * s_smoothed_pm1;
        s_smoothed_pm25 = alpha * pm25 + (1.0f - alpha) * s_smoothed_pm25;
        s_smoothed_pm10 = alpha * pm10 + (1.0f - alpha) * s_smoothed_pm10;
    }
    s_last_update_us = esp_timer_get_time();
}

/* Add a new PM sample, compute median over recent window, then update EWMA. */
static void pms_filter_add_sample(float pm1, float pm25, float pm10)
{
    /* Ring buffers and indices for median calculation */
    static float ring1[CONFIG_IAQ_PMS5003_RING_SIZE];
    static float ring25[CONFIG_IAQ_PMS5003_RING_SIZE];
    static float ring10[CONFIG_IAQ_PMS5003_RING_SIZE];
    static int ring_idx = 0;
    static int ring_count = 0;

    ring1[ring_idx] = pm1;
    ring25[ring_idx] = pm25;
    ring10[ring_idx] = pm10;
    ring_idx = (ring_idx + 1) % CONFIG_IAQ_PMS5003_RING_SIZE;
    if (ring_count < CONFIG_IAQ_PMS5003_RING_SIZE) ring_count++;

    float s1[CONFIG_IAQ_PMS5003_RING_SIZE];
    float s25[CONFIG_IAQ_PMS5003_RING_SIZE];
    float s10[CONFIG_IAQ_PMS5003_RING_SIZE];
    for (int k = 0; k < ring_count; ++k) { s1[k] = ring1[k]; s25[k] = ring25[k]; s10[k] = ring10[k]; }
    qsort(s1, ring_count, sizeof(float), cmp_f);
    qsort(s25, ring_count, sizeof(float), cmp_f);
    qsort(s10, ring_count, sizeof(float), cmp_f);
    float med1 = s1[ring_count/2];
    float med25 = s25[ring_count/2];
    float med10 = s10[ring_count/2];

    portENTER_CRITICAL(&s_pm_lock);
    update_smoothed(med1, med25, med10);
    portEXIT_CRITICAL(&s_pm_lock);
}

/* Snapshot getter with staleness check; returns true if fresh */
static bool pms_filter_get_snapshot(float *out_pm1, float *out_pm25, float *out_pm10,
                                    int64_t stale_timeout_ms, int64_t *out_age_ms)
{
    portENTER_CRITICAL(&s_pm_lock);
    float pm1 = s_smoothed_pm1, pm25 = s_smoothed_pm25, pm10 = s_smoothed_pm10;
    int64_t age_ms = (esp_timer_get_time() - s_last_update_us) / 1000;
    portEXIT_CRITICAL(&s_pm_lock);

    if (out_age_ms) *out_age_ms = age_ms;
    if (age_ms < 0 || age_ms > stale_timeout_ms || isnan(pm25)) {
        return false;
    }
    if (out_pm1)  *out_pm1  = pm1;
    if (out_pm25) *out_pm25 = pm25;
    if (out_pm10) *out_pm10 = pm10;
    return true;
}

static void pms5003_rx_task(void *arg)
{
    uint8_t buf[128];
    int total = 0;
    uart_event_t ev;
    while (s_initialized) {
        if (xQueueReceive(s_uart_queue, &ev, portMAX_DELAY) != pdTRUE) continue;

        /* Handle explicit clear requests (e.g., after reset/flush) */
        if (s_rx_clear_requested) {
            total = 0;
            s_rx_clear_requested = false;
        }

        if (ev.type == UART_FIFO_OVF || ev.type == UART_BUFFER_FULL) {
            /* On overflow/full conditions, flush hardware buffer and drop any partial parse state */
            (void)uart_bus_flush_rx(s_uart_port);
            total = 0;
            /* Continue to next event */
            continue;
        }

        if (ev.type == UART_DATA) {
            size_t available = 0;
            (void)uart_get_buffered_data_len(s_uart_port, &available);
            while (available > 0) {
                int max_can = (int)(sizeof(buf) - (size_t)total);
                if (max_can <= 0) {
                    /* Buffer full without a parsed frame: drop all but last byte to resync */
                    if (total > 1) {
                        buf[0] = buf[total - 1];
                        total = 1;
                    }
                    max_can = (int)(sizeof(buf) - (size_t)total);
                }
                int to_read = (available < (size_t)max_can) ? (int)available : max_can;
                int n = uart_bus_read_bytes(s_uart_port, buf + total, to_read, 0);
                if (n <= 0) break;
                total += n;
                available -= n;

                /* Try to parse frames from buffer, discarding junk until sync */
                while (total >= 32) {
                    int i = 0;
                    bool found = false;
                    for (; i <= total - 32; ++i) {
                        if (buf[i] == 0x42 && buf[i+1] == 0x4D && uart_validate_pms5003_frame(&buf[i])) {
                            float pm1 = NAN, pm25 = NAN, pm10 = NAN;
                            uint64_t t0 = iaq_prof_tic();
                            if (parse_pms_frame(&buf[i], &pm1, &pm25, &pm10)) {
                                pms_filter_add_sample(pm1, pm25, pm10);
                                iaq_prof_toc(IAQ_METRIC_SENSOR_PMS5003_RX, t0);
                            }
                            int remain = total - (i + 32);
                            if (remain > 0) memmove(buf, buf + i + 32, remain);
                            total = remain;
                            found = true;
                            break;
                        }
                    }
                    if (!found) {
                        /* No valid frame found: discard all but last byte to allow resync on next data */
                        if (total > 1) {
                            buf[0] = buf[total - 1];
                            total = 1;
                        }
                        break;
                    }
                }
            }
        }
    }
    vTaskDelete(NULL);
}
#endif /* CONFIG_IAQ_PMS5003_BG_READER */

static void pms_set_work_mode(bool work)
{
    if (!s_use_set) return;
    /* PMS5003 SET pin: Low = Sleep, High = Work (per datasheet) */
    gpio_set_level(s_set_gpio, work ? 1 : 0);
}

static bool parse_pms_frame(const uint8_t *frame, float *pm1, float *pm25, float *pm10)
{
    if (!uart_validate_pms5003_frame(frame)) return false;
    /* Atmospheric environment values (ug/m3) are at bytes 10..15 */
    uint16_t pm1_atm = ((uint16_t)frame[10] << 8) | frame[11];
    uint16_t pm25_atm = ((uint16_t)frame[12] << 8) | frame[13];
    uint16_t pm10_atm = ((uint16_t)frame[14] << 8) | frame[15];
    if (pm1)  *pm1  = (float)pm1_atm;
    if (pm25) *pm25 = (float)pm25_atm;
    if (pm10) *pm10 = (float)pm10_atm;
    return true;
}

/* Build a 7-byte command frame: 0x42 0x4D cmd dataH dataL cksH cksL */
static int pms_send_command(uint8_t cmd, uint16_t data)
{
    uint8_t frame[7];
    frame[0] = 0x42;
    frame[1] = 0x4D;
    frame[2] = cmd;
    frame[3] = (uint8_t)((data >> 8) & 0xFF);
    frame[4] = (uint8_t)(data & 0xFF);
    /* Datasheet: checksum is sum of bytes 0..4 */
    uint16_t sum = (uint16_t)(frame[0] + frame[1] + frame[2] + frame[3] + frame[4]);
    frame[5] = (uint8_t)((sum >> 8) & 0xFF);
    frame[6] = (uint8_t)(sum & 0xFF);
    return uart_bus_write_bytes(s_uart_port, frame, sizeof(frame));
}

esp_err_t pms5003_driver_init(void)
{
    if (s_initialized) {
        ESP_LOGW(TAG, "PMS5003 driver already initialized");
        return ESP_OK;
    }

    uart_port_t uart_port = s_uart_port;
    int tx_gpio = CONFIG_IAQ_PMS5003_TX_GPIO;
    int rx_gpio = CONFIG_IAQ_PMS5003_RX_GPIO;
    int rx_buf_size = CONFIG_IAQ_PMS5003_RX_BUF_SIZE;

    esp_err_t ret;
#ifdef CONFIG_IAQ_PMS5003_BG_READER
    ret = uart_bus_init_with_queue(uart_port, tx_gpio, rx_gpio, 9600, rx_buf_size, 8, &s_uart_queue);
    /* Light-sleep wake on UART RX so background streaming survives PM */
    if (ret == ESP_OK) {
        (void)uart_set_wakeup_threshold(uart_port, 1);
#if CONFIG_PM_ENABLE
        esp_err_t wake_err = esp_sleep_enable_uart_wakeup(uart_port);
        if (wake_err != ESP_OK) {
            ESP_LOGW(TAG, "Failed to enable UART%d wakeup: %s", uart_port, esp_err_to_name(wake_err));
        }
#endif
    }
#else
    ret = uart_bus_init(uart_port, tx_gpio, rx_gpio, 9600, rx_buf_size);
#endif
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize UART for PMS5003: %s", esp_err_to_name(ret));
        return ret;
    }

    /* Optional SET pin configuration */
    if (s_set_gpio >= 0) {
        gpio_config_t io = {
            .pin_bit_mask = 1ULL << s_set_gpio,
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        if (gpio_config(&io) == ESP_OK) {
            s_use_set = true;
            pms_set_work_mode(true); /* Ensure sensor is awake */
            /* Give sensor a brief moment to ensure line is applied */
            vTaskDelay(pdMS_TO_TICKS(10));
        } else {
            s_use_set = false;
            ESP_LOGW(TAG, "Failed to configure SET pin GPIO%d; continuing without SET control", s_set_gpio);
        }
    }

    /* Optional RESET pin configuration (active LOW) */
    if (s_rst_gpio >= 0) {
        gpio_config_t io = {
            .pin_bit_mask = 1ULL << s_rst_gpio,
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        if (gpio_config(&io) == ESP_OK) {
            s_use_rst = true;
            /* Keep RESET inactive (HIGH) */
            gpio_set_level(s_rst_gpio, 1);
        } else {
            s_use_rst = false;
            ESP_LOGW(TAG, "Failed to configure RESET pin GPIO%d; continuing without RESET control", s_rst_gpio);
        }
    }

    /* Flush any stale data */
    (void)uart_bus_flush_rx(uart_port);

#if !CONFIG_IAQ_PMS5003_BG_READER
    /* Switch to passive mode so host controls when frames are emitted */
    pm_guard_lock_no_sleep();
    pm_guard_lock_bus();
    int n = pms_send_command(0xE1, 0x0000); /* passive mode */
    pm_guard_unlock_bus();
    pm_guard_unlock_no_sleep();
    if (n == (int)sizeof(uint8_t) * 7) {
        ESP_LOGI(TAG, "PMS5003: set to passive mode");
    } else {
        ESP_LOGW(TAG, "PMS5003: failed to set passive mode (wrote %d bytes)", n);
    }
#endif

    /* Mark initialized before creating background task so it doesn't exit early */
    s_initialized = true;
#ifdef CONFIG_IAQ_PMS5003_BG_READER
    if (!s_rx_task) {
        (void)uart_set_rx_full_threshold(s_uart_port, 32);
        (void)uart_set_rx_timeout(s_uart_port, 2);
        BaseType_t ok = xTaskCreatePinnedToCore(pms5003_rx_task, "pms5003_rx", 2048, NULL,
                                                tskIDLE_PRIORITY + 2, &s_rx_task,
                                                TASK_CORE_PMS5003_RX);
        if (ok != pdPASS) {
            ESP_LOGE(TAG, "Failed to create PMS5003 RX task");
            return ESP_ERR_NO_MEM;
        }
        /* Register for stack HWM reporting */
        iaq_profiler_register_task("pms5003_rx", s_rx_task, 2048);
    }
#endif
    ESP_LOGI(TAG, "PMS5003 driver initialized (UART%d, SET=%s, RST=%s)",
             uart_port, s_use_set ? "yes" : "no", s_use_rst ? "yes" : "no");
    return ESP_OK;
}

esp_err_t pms5003_driver_read(float *out_pm1_0, float *out_pm2_5, float *out_pm10)
{
    if (!s_initialized) {
        ESP_LOGE(TAG, "PMS5003 driver not initialized");
        return ESP_ERR_INVALID_STATE;
    }

#ifdef CONFIG_IAQ_SIMULATION
    return sensor_sim_read_pm(out_pm1_0, out_pm2_5, out_pm10);
#else
#ifdef CONFIG_IAQ_PMS5003_BG_READER
    /* Non-blocking: just return the latest smoothed sample */
    float pm1 = NAN, pm25 = NAN, pm10 = NAN;
    int64_t age_ms = -1;
    if (!pms_filter_get_snapshot(&pm1, &pm25, &pm10, CONFIG_IAQ_PMS5003_STALE_MS, &age_ms)) {
        ESP_LOGW(TAG, "PMS5003 no fresh sample (%lld ms)", (long long)age_ms);
        if (out_pm1_0) *out_pm1_0 = NAN;
        if (out_pm2_5) *out_pm2_5 = NAN;
        if (out_pm10)  *out_pm10  = NAN;
        return ESP_ERR_TIMEOUT;
    }
    if (out_pm1_0) *out_pm1_0 = pm1;
    if (out_pm2_5) *out_pm2_5 = pm25;
    if (out_pm10)  *out_pm10  = pm10;
    return ESP_OK;
#else
    /* Non-blocking snapshot: drain whatever is buffered and attempt to parse one frame. */
    uint8_t buf[128];
    int total = 0;
    esp_err_t ret = ESP_ERR_TIMEOUT;

    /* Ensure device is awake if SET is available */
    pms_set_work_mode(true);

    /* Keep clocks stable and prevent sleep while we request and read a frame */
    pm_guard_lock_no_sleep();
    pm_guard_lock_bus();

    /* Passive mode: request a single frame */
    (void)uart_bus_flush_rx(s_uart_port); /* drop any partial stream */
    int n = pms_send_command(0xE2, 0x0000); /* read/passive query */
    if (n != 7) {
        ESP_LOGW(TAG, "PMS5003: failed to send query command (wrote %d bytes)", n);
    }

    /* Read with resync: keep sliding until a valid frame is found or timeout elapses */
    const TickType_t frame_deadline = xTaskGetTickCount() + pdMS_TO_TICKS(1000);
    while ((int32_t)(frame_deadline - xTaskGetTickCount()) > 0) {
        size_t space = sizeof(buf) - (size_t)total;
        if (space == 0) {
            /* Buffer full without sync: keep last byte to allow realignment */
            buf[0] = buf[total - 1];
            total = 1;
            space = sizeof(buf) - (size_t)total;
        }

        int r = uart_bus_read_bytes(s_uart_port, buf + total, space, 50);
        if (r > 0) total += r;

        while (total >= 32) {
            for (int i = 0; i <= total - 32; ++i) {
                if (buf[i] == 0x42 && buf[i+1] == 0x4D) {
                    float pm1 = NAN, pm25 = NAN, pm10 = NAN;
                    if (parse_pms_frame(&buf[i], &pm1, &pm25, &pm10)) {
                        if (out_pm1_0) *out_pm1_0 = pm1;
                        if (out_pm2_5) *out_pm2_5 = pm25;
                        if (out_pm10)  *out_pm10  = pm10;
                        ret = ESP_OK;
                        goto exit;
                    }
                }
            }

            /* No full frame yet: keep the tail so a split header can be completed next read */
            if (total > 31) {
                memmove(buf, buf + total - 31, 31);
                total = 31;
            }
            break; /* Need more bytes */
        }
    }

    /* No complete/valid frame ready now */
    if (out_pm1_0) *out_pm1_0 = NAN;
    if (out_pm2_5) *out_pm2_5 = NAN;
    if (out_pm10)  *out_pm10  = NAN;
    ESP_LOGW(TAG, "PMS5003 read timeout (avail=%u bytes)", (unsigned)total);
exit:
    pm_guard_unlock_bus();
    pm_guard_unlock_no_sleep();
    return ret;
#endif /* CONFIG_IAQ_PMS5003_BG_READER */
#endif
}

esp_err_t pms5003_driver_reset(void)
{
    if (!s_initialized) {
        ESP_LOGE(TAG, "PMS5003 driver not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    /* Hardware reset if available; else soft flush/wake */
    if (s_use_rst) {
        /* Active LOW pulse on RESET pin */
        gpio_set_level(s_rst_gpio, 0);
        vTaskDelay(pdMS_TO_TICKS(CONFIG_IAQ_PMS5003_RST_PULSE_MS));
        gpio_set_level(s_rst_gpio, 1);
        vTaskDelay(pdMS_TO_TICKS(CONFIG_IAQ_PMS5003_RST_SETTLE_MS));
        /* Flush any reset noise */
        (void)uart_bus_flush_rx(s_uart_port);
        ESP_LOGI(TAG, "PMS5003 hardware reset pulsed (LOW %d ms, settle %d ms)",
                 CONFIG_IAQ_PMS5003_RST_PULSE_MS, CONFIG_IAQ_PMS5003_RST_SETTLE_MS);
    } else {
        /* Flush RX buffer to clear any stale data */
        (void)uart_bus_flush_rx(s_uart_port);
        ESP_LOGI(TAG, "PMS5003 driver reset (RX flushed)");
    }

#ifdef CONFIG_IAQ_PMS5003_BG_READER
    /* Ask RX task to drop any partial buffer; mark last update as stale */
    s_rx_clear_requested = true;
    portENTER_CRITICAL(&s_pm_lock);
    s_last_update_us = 0;
    portEXIT_CRITICAL(&s_pm_lock);
#endif
#if !CONFIG_IAQ_PMS5003_BG_READER
    /* After reset the sensor returns to active streaming; re-apply passive mode for on-demand reads */
    pms_set_work_mode(true);
    pm_guard_lock_no_sleep();
    pm_guard_lock_bus();
    int n = pms_send_command(0xE1, 0x0000); /* passive mode */
    pm_guard_unlock_bus();
    pm_guard_unlock_no_sleep();
    if (n == (int)sizeof(uint8_t) * 7) {
        ESP_LOGI(TAG, "PMS5003: passive mode re-applied after reset");
    } else {
        ESP_LOGW(TAG, "PMS5003: failed to set passive mode after reset (wrote %d bytes)", n);
    }
#endif

    return ESP_OK;
}

esp_err_t pms5003_driver_deinit(void)
{
    if (!s_initialized) {
        return ESP_OK;
    }

#ifdef CONFIG_IAQ_PMS5003_BG_READER
    if (s_rx_task) {
        /* Stop background RX task before tearing down UART/queue */
        s_initialized = false;
        /* Ensure the task is not left blocked on the queue */
        vTaskDelete(s_rx_task);
        s_rx_task = NULL;
    }
#endif

    esp_err_t ret = uart_bus_deinit(s_uart_port);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to deinitialize UART: %s", esp_err_to_name(ret));
        return ret;
    }

    s_initialized = false;
    ESP_LOGI(TAG, "PMS5003 driver deinitialized");
    return ESP_OK;
}

esp_err_t pms5003_driver_disable(void)
{
    if (!s_initialized) {
        ESP_LOGE(TAG, "PMS5003 driver not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    /* Flush UART buffer to discard any pending data */
    (void)uart_bus_flush_rx(s_uart_port);

    /* Put sensor to sleep via SET pin if available */
    if (s_use_set) {
        /* Ensure we can hold the level during light sleep */
        gpio_hold_dis(s_set_gpio);
        pms_set_work_mode(false);  /* Set to sleep mode (LOW) */
        /* Latch the low level through light sleep so the fan doesn't wake */
        gpio_hold_en(s_set_gpio);
        ESP_LOGI(TAG, "PMS5003 disabled (hardware sleep via SET pin)");
    } else {
        ESP_LOGW(TAG, "PMS5003 disabled (SET pin not configured, no hardware sleep)");
    }

    return ESP_OK;
}

esp_err_t pms5003_driver_enable(void)
{
    if (!s_initialized) {
        ESP_LOGE(TAG, "PMS5003 driver not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    /* Wake sensor from sleep via SET pin if available */
    if (s_use_set) {
        /* Release hold so we can drive the pin high again */
        gpio_hold_dis(s_set_gpio);
        pms_set_work_mode(true);  /* Set to work mode (HIGH) */
        /* Give sensor time to wake up and stabilize */
        vTaskDelay(pdMS_TO_TICKS(100));
        ESP_LOGI(TAG, "PMS5003 enabled (woke from sleep via SET pin)");
    } else {
        ESP_LOGI(TAG, "PMS5003 enabled (SET pin not configured)");
    }

    /* Flush UART buffer to clear any wake-up noise */
    (void)uart_bus_flush_rx(s_uart_port);

    return ESP_OK;
}
