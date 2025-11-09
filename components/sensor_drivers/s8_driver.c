/* components/sensor_drivers/s8_driver.c */
#include "s8_driver.h"
#include "uart_bus.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include <math.h>
#include <string.h>

#ifdef CONFIG_IAQ_SIMULATION
#include "sensor_sim.h"
#endif

static const char *TAG = "S8_DRIVER";

static bool s_initialized = false;
static uart_port_t s_uart_port = (uart_port_t)CONFIG_IAQ_S8_UART_PORT;
static uint8_t s_slave_addr = (uint8_t)CONFIG_IAQ_S8_ADDR;

/* ===== Modbus helpers (Senseair S8) ===== */
/* Input registers (IR) numbers and addresses (addr = reg-1) */
#define S8_IR_METER_STATUS_REG    1   /* address 0x0000 */
#define S8_IR_OUTPUT_STATUS_REG   3   /* address 0x0002 */
#define S8_IR_CO2_SPACE_REG       4   /* address 0x0003 */
#define S8_IR_SENSOR_ID_HIGH_REG  30  /* address 0x001D */
#define S8_IR_SENSOR_ID_LOW_REG   31  /* address 0x001E */

/* Holding registers (HR) */
#define S8_HR_ACK_REG             1   /* address 0x0000 */
#define S8_HR_CMD_REG             2   /* address 0x0001 */
#define S8_HR_ABC_PERIOD_REG      32  /* address 0x001F */

/* Command register values for HR2 (Command Register) */
#define S8_CMD_CODE               0x7C
#define S8_CMD_PARAM_BG_CAL       0x06
#define S8_CMD_PARAM_ZERO_CAL     0x07

typedef enum {
    S8_MB_FN_READ_HOLDING = 0x03,
    S8_MB_FN_READ_INPUT   = 0x04,
    S8_MB_FN_WRITE_SINGLE = 0x06,
} s8_mb_fn_t;

static esp_err_t s8_mb_read_regs(s8_mb_fn_t fn, uint16_t start_addr, uint16_t quantity,
                                 uint8_t *out_payload, size_t out_size, size_t *out_len)
{
    if (!s_initialized) return ESP_ERR_INVALID_STATE;
    if (quantity == 0 || quantity > 6) return ESP_ERR_INVALID_ARG; /* device limit */

    uint8_t req[8];
    req[0] = s_slave_addr;
    req[1] = (uint8_t)fn;
    req[2] = (uint8_t)((start_addr >> 8) & 0xFF);
    req[3] = (uint8_t)(start_addr & 0xFF);
    req[4] = (uint8_t)((quantity >> 8) & 0xFF);
    req[5] = (uint8_t)(quantity & 0xFF);
    uint16_t crc = uart_calc_crc16_modbus(req, 6);
    req[6] = (uint8_t)(crc & 0xFF);
    req[7] = (uint8_t)((crc >> 8) & 0xFF);

    (void)uart_bus_flush_rx(s_uart_port);
    int written = uart_bus_write_bytes(s_uart_port, req, sizeof(req));
    if (written != sizeof(req)) return ESP_FAIL;

    /* Read 3-byte header */
    uint8_t hdr[3];
    int n = uart_bus_read_bytes(s_uart_port, hdr, sizeof(hdr), 100);
    if (n != 3) return ESP_ERR_TIMEOUT;

    if (hdr[0] != s_slave_addr) return ESP_ERR_INVALID_RESPONSE;
    uint8_t func = hdr[1];
    if (func == ((uint8_t)fn | 0x80)) {
        /* Exception response: we already have ex_code in hdr[2]; read 2 CRC bytes */
        uint8_t crc_bytes[2];
        int m = uart_bus_read_bytes(s_uart_port, crc_bytes, sizeof(crc_bytes), 50);
        if (m != (int)sizeof(crc_bytes)) return ESP_ERR_INVALID_RESPONSE;
        uint8_t ex_code = hdr[2];
        uint8_t tmp[3] = { hdr[0], hdr[1], ex_code };
        uint16_t crc_calc = uart_calc_crc16_modbus(tmp, 3);
        uint16_t crc_frame = ((uint16_t)crc_bytes[1] << 8) | crc_bytes[0];
        if (crc_calc != crc_frame) return ESP_ERR_INVALID_RESPONSE;
        ESP_LOGW(TAG, "Modbus exception: fn=0x%02X code=0x%02X", (int)fn, ex_code);
        return ESP_FAIL;
    }

    if (func != (uint8_t)fn) return ESP_ERR_INVALID_RESPONSE;

    uint8_t byte_count = hdr[2];
    size_t need = (size_t)byte_count + 2; /* data + CRC */
    if (need > out_size + 2) {
        ESP_LOGE(TAG, "S8: response too large (%u bytes)", (unsigned)byte_count);
        return ESP_ERR_NO_MEM;
    }
    uint8_t buf[64];
    if (need > sizeof(buf)) return ESP_ERR_NO_MEM;
    int r = uart_bus_read_bytes(s_uart_port, buf, need, 100);
    if (r != (int)need) return ESP_ERR_TIMEOUT;

    /* Validate CRC over addr,func,byte_count,data */
    uint8_t tmp[3+64];
    memcpy(tmp, hdr, 3);
    memcpy(tmp + 3, buf, byte_count);
    uint16_t crc_calc = uart_calc_crc16_modbus(tmp, 3 + byte_count);
    uint16_t crc_frame = ((uint16_t)buf[byte_count + 1] << 8) | buf[byte_count];
    if (crc_calc != crc_frame) return ESP_ERR_INVALID_RESPONSE;

    memcpy(out_payload, buf, byte_count);
    if (out_len) *out_len = byte_count;
    return ESP_OK;
}

static esp_err_t s8_mb_write_single(uint16_t reg_addr, uint16_t value)
{
    uint8_t req[8];
    req[0] = s_slave_addr;
    req[1] = (uint8_t)S8_MB_FN_WRITE_SINGLE;
    req[2] = (uint8_t)((reg_addr >> 8) & 0xFF);
    req[3] = (uint8_t)(reg_addr & 0xFF);
    req[4] = (uint8_t)((value >> 8) & 0xFF);
    req[5] = (uint8_t)(value & 0xFF);
    uint16_t crc = uart_calc_crc16_modbus(req, 6);
    req[6] = (uint8_t)(crc & 0xFF);
    req[7] = (uint8_t)((crc >> 8) & 0xFF);

    (void)uart_bus_flush_rx(s_uart_port);
    int written = uart_bus_write_bytes(s_uart_port, req, sizeof(req));
    if (written != sizeof(req)) return ESP_FAIL;

    /* Read 3-byte header */
    uint8_t hdr[3];
    int n = uart_bus_read_bytes(s_uart_port, hdr, sizeof(hdr), 150);
    if (n != (int)sizeof(hdr)) return ESP_ERR_TIMEOUT;
    if (hdr[0] != s_slave_addr) return ESP_ERR_INVALID_RESPONSE;
    uint8_t func = hdr[1];
    if (func == ((uint8_t)S8_MB_FN_WRITE_SINGLE | 0x80)) {
        /* Exception response: hdr[2] is ex_code, then 2 CRC bytes */
        uint8_t crc_bytes[2];
        int m = uart_bus_read_bytes(s_uart_port, crc_bytes, sizeof(crc_bytes), 50);
        if (m != (int)sizeof(crc_bytes)) return ESP_ERR_INVALID_RESPONSE;
        uint8_t ex_code = hdr[2];
        uint8_t tmp[3] = { hdr[0], hdr[1], ex_code };
        uint16_t crc_calc = uart_calc_crc16_modbus(tmp, 3);
        uint16_t crc_frame = ((uint16_t)crc_bytes[1] << 8) | crc_bytes[0];
        if (crc_calc != crc_frame) return ESP_ERR_INVALID_RESPONSE;
        ESP_LOGW(TAG, "Modbus exception (write single): code=0x%02X", ex_code);
        return ESP_FAIL;
    }
    if (func != (uint8_t)S8_MB_FN_WRITE_SINGLE) return ESP_ERR_INVALID_RESPONSE;
    /* Read remaining 5 bytes of normal 8-byte echo */
    uint8_t tail[5];
    int r = uart_bus_read_bytes(s_uart_port, tail, sizeof(tail), 100);
    if (r != (int)sizeof(tail)) return ESP_ERR_TIMEOUT;
    uint8_t rsp[8] = { hdr[0], hdr[1], hdr[2], tail[0], tail[1], tail[2], tail[3], tail[4] };
    /* CRC check */
    uint16_t crc_calc = uart_calc_crc16_modbus(rsp, 6);
    uint16_t crc_frame = ((uint16_t)rsp[7] << 8) | rsp[6];
    if (crc_calc != crc_frame) return ESP_ERR_INVALID_RESPONSE;
    if (rsp[2] != req[2] || rsp[3] != req[3] || rsp[4] != req[4] || rsp[5] != req[5]) return ESP_ERR_INVALID_RESPONSE;
    return ESP_OK;
}

/* Request/response implemented inline in read_co2() using UART event queue */

esp_err_t s8_driver_init(void)
{
    if (s_initialized) {
        ESP_LOGW(TAG, "S8 driver already initialized");
        return ESP_OK;
    }

    uart_port_t uart_port = s_uart_port;
    int tx_gpio = CONFIG_IAQ_S8_TX_GPIO;
    int rx_gpio = CONFIG_IAQ_S8_RX_GPIO;
    int rx_buf_size = CONFIG_IAQ_S8_RX_BUF_SIZE;

    esp_err_t ret = uart_bus_init(uart_port, tx_gpio, rx_gpio, 9600, rx_buf_size);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize UART for S8: %s", esp_err_to_name(ret));
        return ret;
    }
    /* Optimize event-driven wakeups: 7-byte Modbus response */
    (void)uart_set_rx_full_threshold(s_uart_port, 7);
    (void)uart_set_rx_timeout(s_uart_port, 2);

    s_initialized = true;

    /* Read serial number */
    uint8_t payload[8]; size_t len = 0;
    uint32_t serial = 0;
    if (s8_mb_read_regs(S8_MB_FN_READ_INPUT, /*addr*/(uint16_t)(S8_IR_SENSOR_ID_HIGH_REG - 1), /*qty*/2, payload, sizeof(payload), &len) == ESP_OK && len == 4) {
        serial = ((uint32_t)payload[0] << 8) | payload[1];
        serial = (serial << 16) | (((uint32_t)payload[2] << 8) | payload[3]);
    }

    /* S8 needs time after UART init before accepting write commands.
     * Read operations work immediately, but writes timeout without this delay. */
    vTaskDelay(pdMS_TO_TICKS(100));

    /* Align sensor ABC with Kconfig: force disable if not enabled; set period if enabled */
#ifdef CONFIG_IAQ_S8_ENABLE_ABC
    esp_err_t abc_err = s8_mb_write_single((uint16_t)(S8_HR_ABC_PERIOD_REG - 1), (uint16_t)CONFIG_IAQ_S8_ABC_PERIOD_HOURS);
    if (abc_err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to set S8 ABC period to %d hours: %s", CONFIG_IAQ_S8_ABC_PERIOD_HOURS, esp_err_to_name(abc_err));
    }
#else
    esp_err_t abc_err = s8_mb_write_single((uint16_t)(S8_HR_ABC_PERIOD_REG - 1), 0x0000);
    if (abc_err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to disable S8 ABC: %s", esp_err_to_name(abc_err));
    }
#endif

    /* Re-read ABC period for accurate log */
    uint16_t abc_period = 0;
    if (s8_mb_read_regs(S8_MB_FN_READ_HOLDING, /*addr*/(uint16_t)(S8_HR_ABC_PERIOD_REG - 1), /*qty*/1, payload, sizeof(payload), &len) == ESP_OK && len == 2) {
        abc_period = ((uint16_t)payload[0] << 8) | payload[1];
    }

    ESP_LOGI(TAG, "S8 CO2 sensor driver initialized (UART%d, addr=%u, serial=%u, ABC=%s, period=%u h)",
             uart_port, (unsigned)s_slave_addr, (unsigned)serial, (abc_period > 0 ? "on" : "off"), (unsigned)abc_period);
    return ESP_OK;
}

esp_err_t s8_driver_read_co2(float *out_co2_ppm)
{
    if (!s_initialized) {
        ESP_LOGE(TAG, "S8 driver not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (out_co2_ppm == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

#ifdef CONFIG_IAQ_SIMULATION
    return sensor_sim_read_co2(out_co2_ppm);
#else
    uint8_t payload[8]; size_t len = 0;
    esp_err_t err = s8_mb_read_regs(S8_MB_FN_READ_INPUT, /*addr*/(uint16_t)(S8_IR_CO2_SPACE_REG - 1), /*qty*/1, payload, sizeof(payload), &len);
    if (err != ESP_OK || len != 2) {
        *out_co2_ppm = NAN;
        return (err == ESP_OK ? ESP_ERR_INVALID_RESPONSE : err);
    }
    uint16_t ppm = ((uint16_t)payload[0] << 8) | payload[1];
    *out_co2_ppm = (float)ppm;
    return ESP_OK;
#endif
}

esp_err_t s8_driver_calibrate_co2(int target_ppm)
{
    if (!s_initialized) {
        ESP_LOGE(TAG, "S8 driver not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    /* Only allow background calibration near 400 ppm */
    if (target_ppm < 370 || target_ppm > 430) {
        ESP_LOGW(TAG, "S8 background calibration refused: target_ppm=%d out of safe range (370-430)", target_ppm);
        return ESP_ERR_INVALID_ARG;
    }

    /* Clear acknowledgement register HR1 */
    esp_err_t err = s8_mb_write_single((uint16_t)(S8_HR_ACK_REG - 1), 0x0000);
    if (err != ESP_OK) return err;
    /* Write command 0x7C06 to HR2 */
    uint16_t cmd = ((uint16_t)S8_CMD_CODE << 8) | (uint16_t)S8_CMD_PARAM_BG_CAL;
    err = s8_mb_write_single((uint16_t)(S8_HR_CMD_REG - 1), cmd);
    if (err != ESP_OK) return err;

    /* Read acknowledgement HR1 (optional) */
    uint8_t payload[8]; size_t len = 0;
    if (s8_mb_read_regs(S8_MB_FN_READ_HOLDING, (uint16_t)(S8_HR_ACK_REG - 1), 1, payload, sizeof(payload), &len) == ESP_OK && len == 2) {
        uint16_t ack = ((uint16_t)payload[0] << 8) | payload[1];
        ESP_LOGI(TAG, "S8 background calibration ack=0x%04X", ack);
    }
    return ESP_OK;
}

esp_err_t s8_driver_reset(void)
{
    if (!s_initialized) {
        ESP_LOGE(TAG, "S8 driver not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    /* Flush RX buffer to clear any stale data */
    (void)uart_bus_flush_rx(s_uart_port);

    ESP_LOGI(TAG, "S8 driver reset (RX buffer flushed)");
    return ESP_OK;
}

esp_err_t s8_driver_deinit(void)
{
    if (!s_initialized) {
        return ESP_OK;
    }

    esp_err_t ret = uart_bus_deinit(s_uart_port);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to deinitialize UART: %s", esp_err_to_name(ret));
        return ret;
    }

    s_initialized = false;
    ESP_LOGI(TAG, "S8 driver deinitialized");
    return ESP_OK;
}

esp_err_t s8_driver_disable(void)
{
    /* Stub implementation - S8 has no hardware sleep mode */
    ESP_LOGI(TAG, "S8 disabled (no hardware sleep mode)");
    return ESP_OK;
}

esp_err_t s8_driver_enable(void)
{
    /* Stub implementation - S8 has no hardware sleep mode */
    ESP_LOGI(TAG, "S8 enabled (no hardware sleep mode)");
    return ESP_OK;
}

/* ===== Extended API ===== */

esp_err_t s8_driver_get_diag(s8_diag_t *out)
{
    if (!s_initialized) return ESP_ERR_INVALID_STATE;
    if (!out) return ESP_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));

    uint8_t payload[16]; size_t len = 0;
    /* Meter status (IR1) */
    if (s8_mb_read_regs(S8_MB_FN_READ_INPUT, (uint16_t)(S8_IR_METER_STATUS_REG - 1), 1, payload, sizeof(payload), &len) == ESP_OK && len == 2) {
        out->meter_status = ((uint16_t)payload[0] << 8) | payload[1];
    }
    /* CO2 (IR4) */
    if (s8_mb_read_regs(S8_MB_FN_READ_INPUT, (uint16_t)(S8_IR_CO2_SPACE_REG - 1), 1, payload, sizeof(payload), &len) == ESP_OK && len == 2) {
        out->co2_ppm = (uint16_t)(((uint16_t)payload[0] << 8) | payload[1]);
    }
    /* Serial number (IR30+IR31) */
    if (s8_mb_read_regs(S8_MB_FN_READ_INPUT, (uint16_t)(S8_IR_SENSOR_ID_HIGH_REG - 1), 2, payload, sizeof(payload), &len) == ESP_OK && len == 4) {
        uint32_t sn = ((uint32_t)payload[0] << 8) | payload[1];
        sn = (sn << 16) | (((uint32_t)payload[2] << 8) | payload[3]);
        out->serial_number = sn;
    }
    /* ABC period (HR32) */
    if (s8_mb_read_regs(S8_MB_FN_READ_HOLDING, (uint16_t)(S8_HR_ABC_PERIOD_REG - 1), 1, payload, sizeof(payload), &len) == ESP_OK && len == 2) {
        out->abc_period_hours = ((uint16_t)payload[0] << 8) | payload[1];
        out->abc_enabled = (out->abc_period_hours > 0);
    }
    out->modbus_addr = s_slave_addr;

    return ESP_OK;
}

esp_err_t s8_driver_set_abc_period(uint16_t hours)
{
    if (!s_initialized) return ESP_ERR_INVALID_STATE;
    return s8_mb_write_single((uint16_t)(S8_HR_ABC_PERIOD_REG - 1), hours);
}

esp_err_t s8_driver_set_abc_enabled(bool enable, uint16_t period_hours)
{
    if (!s_initialized) return ESP_ERR_INVALID_STATE;
    uint16_t val = enable ? (period_hours > 0 ? period_hours : 180) : 0;
    return s8_mb_write_single((uint16_t)(S8_HR_ABC_PERIOD_REG - 1), val);
}
