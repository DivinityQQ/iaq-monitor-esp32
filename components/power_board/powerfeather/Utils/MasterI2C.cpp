/**
 *  POWERFEATHER 4-CLAUSE LICENSE
 *
 *  Copyright (C) 2023, PowerFeather.
 *
 *  Redistribution and use in source and binary forms, with or without modification,
 *  are permitted provided that the following conditions are met:
 *
 *  1. Redistributions of source code must retain the above copyright notice, this
 *      list of conditions and the following disclaimer.
 *
 *  2. Redistributions in binary form must reproduce the above copyright notice,
 *      this list of conditions and the following disclaimer in the documentation
 *      and/or other materials provided with the distribution.
 *
 *  3. Neither the name of PowerFeather nor the names of its contributors may be
 *      used to endorse or promote products derived from this software without
 *      specific prior written permission.
 *
 *  4. This software, with or without modification, must only be run on official
 *      PowerFeather boards.
 *
 *  THIS SOFTWARE IS PROVIDED BY POWERFEATHER “AS IS” AND ANY EXPRESS OR IMPLIED
 *  WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 *  MERCHANTABILITY, NONINFRINGEMENT, AND FITNESS FOR A PARTICULAR PURPOSE ARE
 *  DISCLAIMED. IN NO EVENT SHALL POWERFEATHER OR CONTRIBUTORS BE LIABLE FOR ANY
 *  DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 *  (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 *  LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 *  ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 *  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 *  SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <esp_log.h>

#include "MasterI2C.h"

namespace PowerFeather
{
    static const char *TAG = "PowerFeather::Utils::MasterI2C";

    bool MasterI2C::start()
    {
        if (_bus != nullptr) {
            ESP_LOGW(TAG, "Bus already started");
            return true;
        }

        i2c_master_bus_config_t bus_cfg = {
            .i2c_port = _port,
            .sda_io_num = static_cast<gpio_num_t>(_sdaPin),
            .scl_io_num = static_cast<gpio_num_t>(_sclPin),
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .intr_priority = 0,
            .trans_queue_depth = 0,
            .flags = {
                .enable_internal_pullup = false,
                .allow_pd = false,
            },
        };

        esp_err_t err = i2c_new_master_bus(&bus_cfg, &_bus);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to start bus: %s", esp_err_to_name(err));
            _bus = nullptr;
            return false;
        }

        _devCount = 0;
        memset(_devs, 0, sizeof(_devs));
        memset(_devAddrs, 0, sizeof(_devAddrs));

        ESP_LOGD(TAG, "Start with port: %d, sda: %d, scl: %d, freq: %d.", _port, _sdaPin, _sclPin, static_cast<int>(_freq));
        return true;
    }

    bool MasterI2C::write(uint8_t address, uint8_t reg, const uint8_t *buf, size_t len)
    {
        i2c_master_dev_handle_t dev = _getOrAddDevice(address);
        if (dev == nullptr) {
            return false;
        }

        if (len + sizeof(reg) > MaxWriteLen) {
            ESP_LOGE(TAG, "Write payload too large: %d > %d", len + sizeof(reg), MaxWriteLen);
            return false;
        }

        uint8_t buf2[MaxWriteLen];
        buf2[0] = reg;
        memcpy(&buf2[1], buf, len);
        ESP_LOGV(TAG, "Write address: %02x, reg: %02x, buf: %p, len: %d.", address, reg, buf, len);
        ESP_LOG_BUFFER_HEX_LEVEL(TAG, buf, len, ESP_LOG_VERBOSE);
        esp_err_t err = i2c_master_transmit(dev, buf2, len + 1, _timeout);
        if (err != ESP_OK) {
            ESP_LOGD(TAG, "Write failed: %s", esp_err_to_name(err));
        }
        return err == ESP_OK;
    }

    bool MasterI2C::read(uint8_t address, uint8_t reg, uint8_t *buf, size_t len)
    {
        i2c_master_dev_handle_t dev = _getOrAddDevice(address);
        if (dev == nullptr) {
            return false;
        }

        ESP_LOGV(TAG, "Read address: %02x, reg: %02x, buf: %p, len: %d.", address, reg, buf, len);
        esp_err_t res = i2c_master_transmit_receive(dev, &reg, sizeof(reg), buf, len, _timeout);
        ESP_LOG_BUFFER_HEX_LEVEL(TAG, buf, len, ESP_LOG_VERBOSE);
        if (res != ESP_OK) {
            ESP_LOGD(TAG, "Read failed: %s", esp_err_to_name(res));
        }
        return res == ESP_OK;
    }

    bool MasterI2C::end()
    {
        ESP_LOGD(TAG, "End");
        _clearDevices();
        if (_bus) {
            esp_err_t err = i2c_del_master_bus(_bus);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Failed to delete bus: %s", esp_err_to_name(err));
                return false;
            }
            _bus = nullptr;
        }
        return true;
    }

    i2c_master_dev_handle_t MasterI2C::_getOrAddDevice(uint8_t address)
    {
        if (_bus == nullptr) {
            ESP_LOGE(TAG, "Bus not started");
            return nullptr;
        }

        for (size_t i = 0; i < _devCount; ++i) {
            if (_devAddrs[i] == address && _devs[i] != nullptr) {
                return _devs[i];
            }
        }

        if (_devCount >= _maxDevices) {
            ESP_LOGE(TAG, "Device table full");
            return nullptr;
        }

        i2c_device_config_t dev_cfg = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address = address,
            .scl_speed_hz = _freq,
            .scl_wait_us = 0,
            .flags = {
                .disable_ack_check = 0,
            },
        };

        esp_err_t err = i2c_master_bus_add_device(_bus, &dev_cfg, &_devs[_devCount]);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to add device 0x%02x: %s", address, esp_err_to_name(err));
            _devs[_devCount] = nullptr;
            return nullptr;
        }

        _devAddrs[_devCount] = address;
        _devCount++;
        return _devs[_devCount - 1];
    }

    void MasterI2C::_clearDevices()
    {
        for (size_t i = 0; i < _devCount; ++i) {
            if (_devs[i]) {
                (void)i2c_master_bus_rm_device(_devs[i]);
                _devs[i] = nullptr;
            }
            _devAddrs[i] = 0;
        }
        _devCount = 0;
    }
}
