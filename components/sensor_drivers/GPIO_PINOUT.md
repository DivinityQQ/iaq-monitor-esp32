# GPIO Pinout - IAQ Monitor ESP32-S3 PowerFeather

This document defines GPIO ownership for sensor hardware and buses on the **ESP32-S3 PowerFeather** board.

---

## ESP32-S3 PowerFeather Pin Reference

### Complete GPIO Mapping

| Feather Label | GPIO | Capabilities | Notes |
|---------------|------|--------------|-------|
| **A0** | 10 | ADC1_9, Digital, Touch | Analog input |
| **A1** | 9 | ADC1_8, Digital, Touch | Analog input |
| **A2** | 8 | ADC1_7, Digital, Touch | Analog input |
| **A3** | 3 | ADC1_2, Digital, Touch | Strapping pin (JTAG) |
| **A4** | 2 | ADC1_1, Digital, Touch | Analog input |
| **A5** | 1 | ADC1_0, Digital, Touch | Analog input |
| **SCL** | 36 | Digital, I2C | Primary I2C clock |
| **SDA** | 35 | Digital, I2C | Primary I2C data |
| **SCK** | 39 | Digital, SPI | SPI clock |
| **MOSI** | 40 | Digital, SPI | SPI data out |
| **MISO** | 41 | Digital, SPI | SPI data in |
| **RX** | 42 | Digital, UART | Default UART RX |
| **TX** | 44 | Digital, UART | Default UART TX |
| **D5** | 15 | Digital, RTC | General purpose |
| **D6** | 16 | Digital, RTC | General purpose |
| **D7** | 37 | Digital | General purpose |
| **D8** | 6 | Digital, ADC1_5, Touch | General purpose |
| **D9** | 17 | Digital, RTC | General purpose |
| **D10** | 18 | Digital, RTC | General purpose |
| **D11** | 45 | Digital | **Strapping pin** - avoid |
| **D12** | 12 | Digital, ADC2_1, Touch, RTC | General purpose |
| **D13** | 11 | Digital, ADC2_0, Touch, RTC | General purpose |

### Reserved/SDK-Managed Pins

| Label | GPIO | Function | Notes |
|-------|------|----------|-------|
| **ALARM** | 21 | Fuel gauge interrupt | SDK-managed |
| **INT** | 5 | Charger interrupt | SDK-managed |
| **LED** | 46 | User LED (green) | Output only |
| **BTN** | 0 | User button | Strapping pin |
| **EN** | 7 | FeatherWing enable | User-readable |
| **SCL1** | 48 | Secondary I2C (STEMMA QT) | SDK-managed (charger/fuel gauge) |
| **SDA1** | 47 | Secondary I2C (STEMMA QT) | SDK-managed (charger/fuel gauge) |

---

## Power Architecture

### Power Rails

| Rail | Voltage | Max Current | Control | Notes |
|------|---------|-------------|---------|-------|
| **3V3** | 3.3V | 1A (shared) | `Mainboard::enable3V3()` | Switchable load switch |
| **VSQT** | 3.3V | 1A (shared) | `Mainboard::enableVSQT()` | STEMMA QT power; disabling cuts charger/fuel gauge I2C |
| **VS** | VUSB or VDC | 2A | Always on | **WARNING:** Up to 18V if VDC connected |
| **Battery** | 3.0-4.2V | - | Via JST-PH | Not directly on headers |

### 5V Sensor Power Strategy

The PowerFeather does not have a native 5V output. For 5V sensors (PMS5003, Senseair S8):

**Option A: Buck-Boost from 3V3 (Recommended)**
- Connect buck-boost input to **3V3** pin
- Provides software-controllable 5V power via `Mainboard::enable3V3(false)`
- Requires buck-boost that accepts 3.3V input (e.g., Pololu U3V70F5)
- Efficiency: ~85-90%

**Option B: Boost from Battery**
- Tap battery via JST-PH connector or solder point
- Always-on 5V, not software controllable
- Higher efficiency from 3.7V nominal

**Option C: External DC (VDC/VS)**
- If using external 5V DC supply on VDC, VS will output ~5V
- Requires constant external power

---

## Sensor Pin Assignments

### I2C Bus (Shared by SHT4x, BMP280, SGP41)

| Signal | GPIO | Feather Label | Kconfig Variable | Notes |
|--------|------|---------------|------------------|-------|
| SDA | 35 | SDA | `IAQ_I2C_SDA_GPIO` | Primary I2C data |
| SCL | 36 | SCL | `IAQ_I2C_SCL_GPIO` | Primary I2C clock |

**Frequency:** 100 kHz (configurable via `IAQ_I2C_FREQ_HZ`)

**Power:** Connect to switchable **3V3** rail for power management.

**Connected Sensors:**
- **SHT45:** Temperature & Humidity (I2C addr: 0x44)
- **BMP280:** Barometric Pressure (I2C addr: 0x76 or 0x77)
- **SGP41:** VOC & NOx Gas Sensor (I2C addr: 0x59)

---

### UART1 - PMS5003 Particulate Matter Sensor

| Signal | GPIO | Feather Label | Kconfig Variable | Notes |
|--------|------|---------------|------------------|-------|
| TX | 17 | D9 | `IAQ_PMS5003_TX_GPIO` | ESP TX → PMS5003 RX |
| RX | 18 | D10 | `IAQ_PMS5003_RX_GPIO` | ESP RX ← PMS5003 TX |
| SET | 37 | D7 | `IAQ_PMS5003_SET_GPIO` | Optional: Sleep/Wake control |
| RESET | 6 | D8 | `IAQ_PMS5003_RST_GPIO` | Optional: Active LOW reset |

**UART Port:** UART1 (`IAQ_PMS5003_UART_PORT`)
**Baud Rate:** 9600 bps (fixed)
**Protocol:** 32-byte frames with 16-bit checksum
**RX Buffer:** 256 bytes (default)

**Power:** 5V from buck-boost converter (input from 3V3 rail)

---

### UART2 - Senseair S8 CO₂ Sensor

| Signal | GPIO | Feather Label | Kconfig Variable | Notes |
|--------|------|---------------|------------------|-------|
| TX | 15 | D5 | `IAQ_S8_TX_GPIO` | ESP TX → S8 RX |
| RX | 16 | D6 | `IAQ_S8_RX_GPIO` | ESP RX ← S8 TX |

**UART Port:** UART2 (`IAQ_S8_UART_PORT`)
**Baud Rate:** 9600 bps (fixed)
**Protocol:** Modbus RTU with CRC16
**RX Buffer:** 256 bytes (default)

**Power:** 5V from buck-boost converter (input from 3V3 rail)

**Voltage levels:** Senseair S8 UART is typically 3.3V compatible on RX. Verify your module's TX level; add a resistor divider (10kΩ/20kΩ) if TX outputs 5V.

---

### Alternative UART Assignment (Using Default Feather UART)

If you prefer using the Feather-standard TX/RX pins for Senseair S8:

| Signal | GPIO | Feather Label | Notes |
|--------|------|---------------|-------|
| TX | 44 | TX | ESP TX → S8 RX |
| RX | 42 | RX | ESP RX ← S8 TX |

**Note:** GPIO43 (TX0) is used for console output. Avoid using GPIO43 for sensors.

---

## Power Management Integration

### Controlling Sensor Power

Use the `power_board` component wrapper (see [POWERFEATHER_INTEGRATION.md](../power_board/POWERFEATHER_INTEGRATION.md)):

```c
#include "power_board.h"

// Initialize during startup (called from main.c)
power_board_init();

// Cut power to 3V3 sensors and buck-boost input
power_board_enable_3v3(false);

// Restore power
power_board_enable_3v3(true);
```

---

## UART Buffer Size Requirements

**CRITICAL:** ESP-IDF UART driver requires RX buffer size **> 128 bytes** (hardware FIFO size on ESP32-S3).

- **Minimum valid size:** 256 bytes
- **Kconfig validation:** Range enforced as `256-2048` bytes
- **Runtime check:** `uart_bus_init()` validates and returns `ESP_ERR_INVALID_ARG` if buffer ≤ 128

---

## PowerFeather Reserved/Special Pins

**Do NOT use these GPIOs for sensors:**

| GPIO | Usage | Notes |
|------|-------|-------|
| 0 | User Button (BTN) | Strapping pin, boot mode |
| 3 | A3 | Strapping pin (JTAG) |
| 5 | Charger Interrupt (INT) | SDK-managed |
| 7 | Enable (EN) | FeatherWing enable input |
| 19-20 | USB D-/D+ | Native USB |
| 21 | Fuel Gauge Alarm (ALARM) | SDK-managed |
| 26-32 | SPI Flash / PSRAM | **Never reassign** |
| 43 | TX0 | Console output |
| 45 | D11 | Strapping pin (VDD_SPI) |
| 46 | User LED | Output only |
| 47-48 | SCL1/SDA1 | SDK I2C for charger/fuel gauge |

**Safe GPIOs for Expansion:**
- GPIO 1, 2, 6, 8-12, 15-18, 35-42, 44

---

## Pin Conflict Warnings

- **GPIO47/48:** Reserved for PowerFeather SDK (charger/fuel gauge I2C). Do NOT use for Senseair S8.
- **GPIO45:** Strapping pin for VDD_SPI voltage. Avoid for general use.
- **GPIO46:** Connected to onboard LED. Can be used but LED will respond to signal.
- **VSQT:** Disabling VSQT via SDK cuts communication to battery charger/fuel gauge.

---

## Wiring Summary

```
ESP32-S3 PowerFeather                    Sensors
========================                 =======

3V3 (switchable) ──────┬──────────────→ SHT45 VCC
                       ├──────────────→ BMP280 VCC
                       ├──────────────→ SGP41 VCC
                       └──→ [Buck-Boost] ──→ 5V ──┬──→ PMS5003 VCC
                                                  └──→ S8 VCC

GND ───────────────────┬──────────────→ All sensor GND
                       └──→ [Buck-Boost GND]

GPIO35 (SDA) ──────────┬──────────────→ SHT45 SDA
                       ├──────────────→ BMP280 SDA
                       └──────────────→ SGP41 SDA

GPIO36 (SCL) ──────────┬──────────────→ SHT45 SCL
                       ├──────────────→ BMP280 SCL
                       └──────────────→ SGP41 SCL

GPIO17 (D9) ───────────────────────────→ PMS5003 RX
GPIO18 (D10) ──────────────────────────→ PMS5003 TX
GPIO37 (D7) ───────────────────────────→ PMS5003 SET (optional)
GPIO6 (D8) ────────────────────────────→ PMS5003 RESET (optional)

GPIO15 (D5) ───────────────────────────→ S8 RX (UART_RXD)
GPIO16 (D6) ───────────────────────────→ S8 TX (UART_TXD)
```

---

## Changing Pin Assignments

All pin assignments are controlled via **Kconfig** (`idf.py menuconfig`):

```
Sensor Hardware Configuration
  ├─ I2C Bus Configuration
  │    ├─ SDA GPIO (default: 35)
  │    └─ SCL GPIO (default: 36)
  └─ UART Configuration
       ├─ PMS5003 Particulate Matter Sensor
       │    ├─ TX GPIO (default: 17)
       │    ├─ RX GPIO (default: 18)
       │    ├─ SET GPIO (default: 37)
       │    └─ RESET GPIO (default: 6)
       └─ Senseair S8 CO2 Sensor
            ├─ TX GPIO (default: 15)
            └─ RX GPIO (default: 16)
```

After changing pins:
1. Run `idf.py reconfigure` to regenerate `sdkconfig`
2. Rebuild: `idf.py build`
3. Reflash: `idf.py flash`

---

## References

- [PowerFeather Documentation](https://docs.powerfeather.dev/)
- [PowerFeather-SDK ESP Component](https://components.espressif.com/components/powerfeather/powerfeather-sdk)
- [PowerFeather GitHub](https://github.com/PowerFeather)
- [ESP32-S3 Datasheet](https://www.espressif.com/sites/default/files/documentation/esp32-s3_datasheet_en.pdf)
