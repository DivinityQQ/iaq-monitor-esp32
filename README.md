# IAQ Monitor - ESP32-S3

Indoor Air Quality Monitor using ESP-IDF and FreeRTOS.

## Features (Current)
- ✅ WiFi connectivity with automatic reconnection
- ✅ MQTT client with Home Assistant auto-discovery
- ✅ Console commands for runtime debugging
- ✅ Status LED indication
- ✅ System monitoring (heap, uptime, WiFi RSSI)

## Features (Planned)
- [ ] SHT41 temperature/humidity sensor
- [ ] PMS5003 particulate matter sensor
- [ ] Senseair S8 CO2 sensor
- [ ] SGP41 VOC/NOx sensor
- [ ] BMP280 pressure sensor
- [ ] OLED display
- [ ] Web configuration portal
- [ ] Sensor data compensation
- [ ] Historical data storage

## Prerequisites

- ESP-IDF v5.1 or later
- ESP32-S3 development board
- USB cable for flashing and monitoring

## Installation

1. **Install ESP-IDF:**
```bash
mkdir ~/esp
cd ~/esp
git clone -b v5.1.2 --recursive https://github.com/espressif/esp-idf.git
cd esp-idf
./install.sh esp32s3
```

2. **Clone this repository:**
```bash
git clone https://github.com/yourusername/iaq-monitor-esp32.git
cd iaq-monitor-esp32
```

3. **Set up ESP-IDF environment:**
```bash
. ~/esp/esp-idf/export.sh
```

## Configuration

1. **Run menuconfig:**
```bash
idf.py menuconfig
```

2. **Configure WiFi and MQTT:**
- Navigate to `IAQ Monitor Configuration`
- Set your WiFi SSID and password
- Set your MQTT broker URL
- Configure device ID and other options

## Building and Flashing

1. **Build the project:**
```bash
idf.py build
```

2. **Flash to ESP32-S3:**
```bash
idf.py -p /dev/ttyUSB0 flash
```
(Replace `/dev/ttyUSB0` with your serial port)

3. **Monitor output:**
```bash
idf.py -p /dev/ttyUSB0 monitor
```

4. **Combined flash and monitor:**
```bash
idf.py -p /dev/ttyUSB0 flash monitor
```

## Console Commands

Once running, press Enter to activate the console and use these commands:

- `help` - Show available commands
- `status` - Display system status
- `restart` - Restart the system

## MQTT Topics

The device publishes to these MQTT topics:

- `{device_id}/status` - System status (JSON)
- `{device_id}/state` - Sensor data (JSON)
- `{device_id}/availability` - Online/Offline (LWT)

And subscribes to:
- `{device_id}/cmd/+` - Command topics

## Home Assistant Integration

The device automatically publishes discovery messages for Home Assistant. Sensors will appear automatically in HA once the device connects to MQTT.

## LED Status Indicators

- **3 blinks** - System starting
- **2 blinks** - WiFi connected
- **1 blink** - MQTT connected

## Project Structure

```
├── main/               # Main application
├── components/         # Custom components
│   ├── connectivity/   # WiFi and MQTT
│   └── sensor_drivers/ # Sensor implementations (future)
├── partitions.csv      # Flash partition table
└── sdkconfig.defaults  # Default configuration
```

## Troubleshooting

1. **WiFi won't connect:**
   - Check SSID and password in menuconfig
   - Ensure router is 2.4GHz (ESP32 doesn't support 5GHz)
   - Check serial output for error messages

2. **MQTT won't connect:**
   - Verify broker URL format: `mqtt://ip:port`
   - Check if broker requires authentication
   - Ensure broker is reachable from ESP32 network

3. **Build errors:**
   - Run `idf.py fullclean` and rebuild
   - Ensure ESP-IDF environment is properly sourced
   - Check ESP-IDF version compatibility

## Next Steps

To add sensors:

1. Implement driver in `components/sensor_drivers/`
2. Add sensor task in `main.c`
3. Update MQTT publishing to include sensor data
4. Add Home Assistant discovery for new sensors

## License

MIT License - see LICENSE file for details