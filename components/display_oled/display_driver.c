/* components/display_oled/display_driver.c */
#include "display_oled/display_driver.h"
#include "i2c_bus.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "sdkconfig.h"
#include "esp_check.h"

#if CONFIG_IAQ_OLED_ENABLE

static const char *TAG = "OLED_DRV";

#ifndef CONFIG_IAQ_OLED_I2C_ADDR
#define CONFIG_IAQ_OLED_I2C_ADDR 60
#endif

static i2c_master_dev_handle_t s_dev = NULL;
static bool s_inited = false;
static uint8_t s_column_offset = 0;
static uint8_t s_contrast = 96;
static uint8_t s_rot_180 = 0; /* 0 or 1 */

static esp_err_t sh1106_tx(const uint8_t *bytes, size_t len)
{
    if (!s_dev) return ESP_ERR_INVALID_STATE;
    return i2c_master_transmit(s_dev, bytes, len, CONFIG_IAQ_I2C_TIMEOUT_MS);
}

static esp_err_t sh1106_cmd1(uint8_t cmd)
{
    uint8_t buf[2] = { 0x00, cmd };
    return sh1106_tx(buf, sizeof(buf));
}

static esp_err_t sh1106_cmds(const uint8_t *cmds, size_t n)
{
    if (n == 0) return ESP_OK;
    uint8_t buf[1 + 16];
    while (n) {
        size_t chunk = n > 16 ? 16 : n;
        buf[0] = 0x00; /* command stream */
        for (size_t i = 0; i < chunk; ++i) buf[1 + i] = cmds[i];
        esp_err_t ret = sh1106_tx(buf, 1 + chunk);
        if (ret != ESP_OK) return ret;
        cmds += chunk; n -= chunk;
    }
    return ESP_OK;
}

static esp_err_t sh1106_data(const uint8_t *data, size_t n)
{
    if (!s_dev) return ESP_ERR_INVALID_STATE;
    /* Data stream: control byte 0x40 followed by up to 128 bytes */
    uint8_t buf[1 + 128];
    while (n) {
        size_t chunk = n > 128 ? 128 : n;
        buf[0] = 0x40;
        for (size_t i = 0; i < chunk; ++i) buf[1 + i] = data[i];
        esp_err_t ret = sh1106_tx(buf, 1 + chunk);
        if (ret != ESP_OK) return ret;
        data += chunk; n -= chunk;
    }
    return ESP_OK;
}

static esp_err_t sh1106_set_page_col(uint8_t page, uint8_t col)
{
    /* SH1106 page addressing: B0..B7, then set column (low 4 bits, then high 4 bits). */
    uint8_t cmd[3];
    cmd[0] = (uint8_t)(0xB0 | (page & 0x07));
    uint8_t c = (uint8_t)(col & 0x7F);
    cmd[1] = (uint8_t)(0x00 | (c & 0x0F));
    cmd[2] = (uint8_t)(0x10 | ((c >> 4) & 0x0F));
    return sh1106_cmds(cmd, sizeof(cmd));
}

esp_err_t display_driver_init(void)
{
    if (s_inited) return ESP_OK;

    s_column_offset = (uint8_t)CONFIG_IAQ_OLED_COLUMN_OFFSET;
    s_contrast = (uint8_t)CONFIG_IAQ_OLED_CONTRAST;
    s_rot_180 = 0;
#ifdef CONFIG_IAQ_OLED_ROTATION_180
    s_rot_180 = 1;
#endif

    ESP_RETURN_ON_ERROR(i2c_bus_init(), TAG, "I2C bus init failed");

    uint8_t addr = (uint8_t)CONFIG_IAQ_OLED_I2C_ADDR;
    esp_err_t ret = i2c_bus_add_device(addr, CONFIG_IAQ_I2C_FREQ_HZ, &s_dev);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add SH1106 at 0x%02X: %s", addr, esp_err_to_name(ret));
        return ret;
    }

    /* Basic init sequence */
    const uint8_t init1[] = {
        0xAE,             /* Display OFF */
        0xD5, 0x80,       /* Display clock divide ratio/oscillator */
        0xA8, 0x3F,       /* Multiplex ratio: 1/64 */
        0xD3, 0x00,       /* Display offset */
        0x40,             /* Start line = 0 */
        0xAD, 0x8B,       /* DC-DC control: enable */
        /* Segment remap & COM scan dir for rotation */
    };
    ESP_RETURN_ON_ERROR(sh1106_cmds(init1, sizeof(init1)), TAG, "init stage 1");

    if (s_rot_180) {
        const uint8_t rot[] = { 0xA0, /* seg remap normal */ 0xC0 /* COM scan normal */ };
        ESP_RETURN_ON_ERROR(sh1106_cmds(rot, sizeof(rot)), TAG, "rot 180");
    } else {
        const uint8_t rot[] = { 0xA1, /* seg remap mirror */ 0xC8 /* COM scan remap */ };
        ESP_RETURN_ON_ERROR(sh1106_cmds(rot, sizeof(rot)), TAG, "rot 0");
    }

    const uint8_t init2[] = {
        0xDA, 0x12,       /* COM pins hardware config */
        0x81,             /* Contrast */
    };
    ESP_RETURN_ON_ERROR(sh1106_cmds(init2, sizeof(init2)), TAG, "init stage 2");
    ESP_RETURN_ON_ERROR(sh1106_cmd1(s_contrast), TAG, "contrast");

    const uint8_t init3[] = {
        0xD9, 0x22,       /* Pre-charge period */
        0xDB, 0x30,       /* VCOMH deselect level */
        0xA4,             /* Resume to RAM content */
        0xA6,             /* Normal display */
        0xAF              /* Display ON */
    };
    ESP_RETURN_ON_ERROR(sh1106_cmds(init3, sizeof(init3)), TAG, "init stage 3");

    s_inited = true;
    ESP_LOGI(TAG, "SH1106 initialized (addr=0x%02X, col_off=%u, rot=%u, contrast=%u)",
             addr, (unsigned)s_column_offset, (unsigned)(s_rot_180 ? 180 : 0), (unsigned)s_contrast);
    return ESP_OK;
}

esp_err_t display_driver_power(bool on)
{
    if (!s_dev) return ESP_ERR_INVALID_STATE;
    return sh1106_cmd1(on ? 0xAF : 0xAE);
}

esp_err_t display_driver_set_contrast(uint8_t contrast)
{
    if (!s_dev) return ESP_ERR_INVALID_STATE;
    uint8_t seq[2] = { 0x81, contrast };
    s_contrast = contrast;
    return sh1106_cmds(seq, sizeof(seq));
}

esp_err_t display_driver_set_invert(bool invert)
{
    return sh1106_cmd1(invert ? 0xA7 : 0xA6);
}

esp_err_t display_driver_set_rotation(int degrees)
{
    if (!s_dev) return ESP_ERR_INVALID_STATE;
    uint8_t seg = 0xA1, com = 0xC8; /* default 0° */
    if (degrees == 180) { seg = 0xA0; com = 0xC0; }
    uint8_t seq[2] = { seg, com };
    s_rot_180 = (degrees == 180) ? 1 : 0;
    return sh1106_cmds(seq, sizeof(seq));
}

esp_err_t display_driver_write_page(uint8_t page, const uint8_t *data128)
{
    if (!s_dev) return ESP_ERR_INVALID_STATE;
    ESP_RETURN_ON_ERROR(sh1106_set_page_col(page, s_column_offset), TAG, "set page/col");
    return sh1106_data(data128, 128);
}

#else /* CONFIG_IAQ_OLED_ENABLE */

/* No-op stubs when OLED is disabled */
esp_err_t display_driver_init(void) { return ESP_OK; }
esp_err_t display_driver_power(bool on) { (void)on; return ESP_OK; }
esp_err_t display_driver_set_contrast(uint8_t c) { (void)c; return ESP_OK; }
esp_err_t display_driver_set_invert(bool inv) { (void)inv; return ESP_OK; }
esp_err_t display_driver_set_rotation(int deg) { (void)deg; return ESP_OK; }
esp_err_t display_driver_write_page(uint8_t page, const uint8_t *data128) { (void)page; (void)data128; return ESP_OK; }

#endif /* CONFIG_IAQ_OLED_ENABLE */
