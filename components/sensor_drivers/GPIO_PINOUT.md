# GPIO Pinout - IAQ Monitor ESP32-S3

This document defines GPIO ownership for sensor hardware and buses.

## Pin Assignments

### I2C Bus (Shared by SHT4x, BMP280, SGP41)

| Signal | GPIO | Kconfig Variable         | Notes                          |
|--------|------|--------------------------|--------------------------------|
| SDA    | 8    | `IAQ_I2C_SDA_GPIO`       | I2C Serial Data Line           |
| SCL    | 9    | `IAQ_I2C_SCL_GPIO`       | I2C Serial Clock Line          |

**Frequency:** 100 kHz (configurable via `IAQ_I2C_FREQ_HZ`)

**Connected Sensors:**
- **SHT45:** Temperature & Humidity (I2C addr: 0x44)
- **BMP280:** Barometric Pressure (I2C addr: 0x76 or 0x77)
- **SGP41:** VOC & NOx Gas Sensor (I2C addr: 0x59)

---

### UART1 - PMS5003 Particulate Matter Sensor

| Signal | GPIO | Kconfig Variable         | Notes                          |
|--------|------|--------------------------|--------------------------------|
| TX     | 17   | `IAQ_PMS5003_TX_GPIO`    | ESP TX → PMS5003 RX            |
| RX     | 18   | `IAQ_PMS5003_RX_GPIO`    | ESP RX ← PMS5003 TX            |
| SET    | -1   | `IAQ_PMS5003_SET_GPIO`   | Optional: Sleep/Wake control   |
| RESET  | -1   | `IAQ_PMS5003_RST_GPIO`   | Optional: Active LOW reset     |

**UART Port:** UART1 (`IAQ_PMS5003_UART_PORT`)
**Baud Rate:** 9600 bps (fixed)
**Protocol:** 32-byte frames with 16-bit checksum
**RX Buffer:** 256 bytes (default)

If RESET is enabled (`IAQ_PMS5003_RST_GPIO` ≥ 0):
- The driver pulses RESET LOW for `IAQ_PMS5003_RST_PULSE_MS` (default 50 ms), then HIGH
- Waits `IAQ_PMS5003_RST_SETTLE_MS` (default 200 ms) before resuming reads
- Coordinator warm-up timer still governs when values are considered READY

---

### UART2 - Senseair S8 CO₂ Sensor

| Signal | GPIO | Kconfig Variable     | Notes                          |
|--------|------|----------------------|--------------------------------|
| TX     | 19   | `IAQ_S8_TX_GPIO`     | ESP TX → S8 RX                 |
| RX     | 20   | `IAQ_S8_RX_GPIO`     | ESP RX ← S8 TX                 |

**UART Port:** UART2 (`IAQ_S8_UART_PORT`)
**Baud Rate:** 9600 bps (fixed)
**Protocol:** Modbus RTU with CRC16
**RX Buffer:** 256 bytes (default)

---

### UART Buffer Size Requirements

**CRITICAL:** ESP-IDF UART driver requires RX buffer size **> 128 bytes** (hardware FIFO size on ESP32-S3).

- **Minimum valid size:** 256 bytes
- **Kconfig validation:** Range enforced as `256-2048` bytes
- **Runtime check:** `uart_bus_init()` validates and returns `ESP_ERR_INVALID_ARG` if buffer ≤ 128

If you encounter `uart rx buffer length error`, increase the buffer size in `idf.py menuconfig`:
```
Sensor Hardware Configuration → UART Configuration → [Sensor] → RX Buffer Size
```

---

## ESP32-S3 Reserved/Special Pins

**Do NOT use these GPIOs for sensors:**

| GPIO Range | Usage                          | Notes                                   |
|------------|--------------------------------|-----------------------------------------|
| 0          | Boot Mode / Strapping          | Pull-up, avoid unless needed           |
| 3          | JTAG / Strapping               | Used during programming                 |
| 19-20      | USB D-/D+ (on DevKitC-1)       | **Safe if not using USB-OTG**           |
| 26-32      | SPI Flash / PSRAM              | **Never reassign** (bootloop risk)      |
| 33-37      | Internal (not broken out)      | N/A                                     |
| 45         | Strapping Pin                  | Avoid unless configured properly        |
| 46         | Strapping / Boot Mode          | Avoid unless configured properly        |

**Safe GPIO Ranges for Expansion:**
- GPIO 1, 2, 4-7, 10-16, 21, 38-42, 47-48

---

## Power Sequencing Notes

- **No external enable pins** required for breakout modules (self-powered).
- **PMS5003 SET pin** (optional): Controls sleep/wake to extend sensor lifespan.
  - HIGH = Active, LOW = Sleep
  - Set to `-1` in Kconfig to disable (always-on mode).

---

## Pin Conflict Warnings

- **UART0 (GPIO43/44):** Reserved for console, do not reassign.
- **I2C vs UART:** Current defaults (GPIO 8/9 for I2C, 17-20 for UART) have no conflicts.
- **USB-OTG:** If using GPIO19/20 for S8, disable USB-OTG in `sdkconfig`.

---

## Changing Pin Assignments

All pin assignments are controlled via **Kconfig** (`idf.py menuconfig`):

```
Sensor Hardware Configuration
  ├─ I2C Bus Configuration
  └─ UART Configuration
      ├─ PMS5003 Particulate Matter Sensor
      └─ Senseair S8 CO2 Sensor
```

After changing pins:
1. Run `idf.py reconfigure` to regenerate `sdkconfig`
2. Rebuild: `idf.py build`
3. Reflash: `idf.py flash`

---

## References

- [ESP32-S3 Datasheet](https://www.espressif.com/sites/default/files/documentation/esp32-s3_datasheet_en.pdf)
- [ESP32-S3-DevKitC-1 Schematic](https://dl.espressif.com/dl/schematics/SCH_ESP32-S3-DEVKITC-1_V1.1_20221130.pdf)
- CLAUDE.md - Sensor driver architecture
