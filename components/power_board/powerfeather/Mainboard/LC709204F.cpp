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

#include <math.h>

#include <esp_log.h>

#include <Utils/Util.h>
#include "LC709204F.h"

namespace PowerFeather
{
    static const char *TAG = "PowerFeather::Mainboard::LC709204F";

    bool LC709204F::_readReg(Registers reg, uint16_t &data)
    {
        uint8_t reply[6];
        reply[0] = _i2cAddress << 1;          // write byte
        reply[1] = static_cast<uint8_t>(reg); // register
        reply[2] = reply[0] | 0x1;            // read byte

        if (!_i2c.read(_i2cAddress, reply[1], &(reply[3]), 3))
        {
            ESP_LOGD(TAG, "Read register %02x failed.", reply[1]);
            return false;
        }

        uint8_t crc = _computeCRC8(reply, 5);

        if (crc != reply[5]) // crc failed
        {
            ESP_LOGD(TAG, "CRC %02x different from expected %02x.", reply[5], crc);
            return false;
        }

        data = reply[4];
        data <<= 8;
        data |= reply[3];

        ESP_LOGD(TAG, "Read register %02x succeeded, crc: %02x  value: %04x", reply[1], crc, data);
        return true;
    }

    bool LC709204F::_writeReg(Registers reg, uint16_t data)
    {
        uint8_t send[5];
        send[0] = _i2cAddress << 1;
        send[1] = static_cast<uint8_t>(reg);
        send[2] = data & 0xFF;
        send[3] = data >> 8;
        send[4] = _computeCRC8(send, 4);
        ESP_LOGD(TAG, "Write register %02x, crc: %02x  value: %04x", send[1], send[4], data);
        return _i2c.write(_i2cAddress, send[1], &(send[2]), 3);
    }

    uint8_t LC709204F::_computeCRC8(uint8_t *data, int len)
    {
        const uint8_t polynomial = 0x07;
        uint8_t crc = 0x00;

        for (int j = len; j; --j)
        {
            crc ^= *data++;

            for (int i = 8; i; --i)
            {
                crc = (crc & 0x80) ? (crc << 1) ^ polynomial : (crc << 1);
            }
        }
        return crc;
    }

    bool LC709204F::_setVoltageAlarm(Registers reg, uint16_t voltage)
    {
        if (voltage == 0 || (voltage >= LC709204F::MinVoltageAlarm && voltage <= LC709204F::MaxVoltageAlarm))
        {
            return _writeReg(reg, voltage);
        }
        return false;
    }

    bool LC709204F::_clearAlarm(BatteryStatus alarm)
    {
        uint16_t value = 0;
        if (_readReg(Registers::BatteryStatus, value))
        {
            value &= ~(0b1 << static_cast<uint8_t>(alarm));
            return _writeReg(Registers::BatteryStatus, value);
        }
        return false;
    }

    bool LC709204F::getOperationMode(bool &enabled)
    {
        uint16_t value = 0;
        bool res = _readReg(Registers::IC_Power_Mode, value);
        enabled = (value == static_cast<uint16_t>(OperationMode::OperationalMode));
        return res;
    }

    bool LC709204F::getCellVoltage(uint16_t &voltage)
    {
        return _readReg(Registers::Cell_Voltage, voltage);
    }

    bool LC709204F::getRSOC(uint8_t &percent)
    {
        uint16_t value = 0;
        if (_readReg(Registers::RSOC, value))
        {
            percent = value;
            return true;
        }
        return false;
    }

    bool LC709204F::getTimeToEmpty(uint16_t &minutes)
    {
        return _readReg(Registers::TimeToEmpty, minutes);
    }

    bool LC709204F::getTimeToFull(uint16_t &minutes)
    {
        return _readReg(Registers::TimeToFull, minutes);
    }

    bool LC709204F::getCellTemperature(float& temperature)
    {
        uint16_t value = 0;
        if (_readReg(Registers::TSENSE1, value))
        {
            temperature = Util::fromRaw(value, 0.1, 0x0DCC) + 80.0f;
            return true;
        }
        return false;
    }

    bool LC709204F::getCycles(uint16_t &cycles)
    {
        return _readReg(Registers::Cycle_Count, cycles);
    }

    bool LC709204F::getSOH(uint8_t &percent)
    {
        uint16_t value = 0;
        if (_readReg(Registers::State_Of_Health, value))
        {
            percent = value;
            return true;
        }
        return false;
    }

    bool LC709204F::getInitialized(bool& state)
    {
        uint16_t value = 0;
        if (_readReg(Registers::BatteryStatus, value))
        {
            state = !(value & (0b1 << static_cast<uint8_t>(BatteryStatus::Initialized)));
            return true;
        }
        return false;
    }

    bool LC709204F::setOperationMode(bool enable)
    {
        uint16_t value = static_cast<uint16_t>(enable ? OperationMode::OperationalMode : OperationMode::SleepMode);
        return _writeReg(Registers::IC_Power_Mode, value);
    }

    bool LC709204F::setAPA(uint16_t capacity, ChangeOfParameter changeOfParam)
    {
        if (capacity >= MinBatteryCapacity && capacity <= MaxBatteryCapacity)
        {
            if (changeOfParam == ChangeOfParameter::ICR18650_26H)
            {
                return _writeReg(Registers::APA, 0x0606);
            }
            else if (changeOfParam == ChangeOfParameter::UR18650ZY)
            {
                return _writeReg(Registers::APA, 0x1010);
            }
            else
            {
                auto prev = _apaTable[0];
                for (int i = 0; i < sizeof(_apaTable) / sizeof(prev); i++)
                {
                    auto cur = _apaTable[i];
                    uint16_t cap = std::get<0>(cur);
                    uint16_t apa = 0;

                    if (capacity == cap)
                    {
                        apa = (std::get<1>(cur) << 8) | std::get<1>(cur);
                    }
                    else
                    {
                        auto prev = _apaTable[i - 1];
                        uint16_t prev_cap = std::get<0>(prev);
                        if (capacity < cap && (prev != cur && cap > prev_cap))
                        {
                            float res = round(std::get<1>(prev) + (std::get<1>(cur) - std::get<1>(prev)) * ((static_cast<float>(capacity) - prev_cap) / (cap - prev_cap)));
                            apa = (static_cast<uint8_t>(res) << 8) | static_cast<uint8_t>(res);
                        }
                    }

                    if (apa)
                    {
                        return _writeReg(Registers::APA, apa);
                    }

                    prev = cur;
                }
            }
        }
        return false;
    }

    bool LC709204F::setChangeOfParameter(ChangeOfParameter changeOfParam)
    {
        return _writeReg(Registers::Change_Of_The_Parameter, static_cast<uint16_t>(changeOfParam));
    }

    bool LC709204F::setCellTemperature(float temperature)
    {
        uint16_t value = Util::toRaw(temperature - 80.0f, 0.1f, 0xDCC);
        return _writeReg(Registers::TSENSE1, value);
    }

    bool LC709204F::enableTSENSE(bool enableTsense1, bool enableTsense2)
    {
        uint16_t status = enableTsense1 << 0 | enableTsense2 << 1;
        return _writeReg(Registers::Status_Bit, status);
    }

    bool LC709204F::setLowVoltageAlarm(uint16_t voltage)
    {
        return _setVoltageAlarm(Registers::Alarm_Low_Cell_Voltage, voltage);
    }

    bool LC709204F::setHighVoltageAlarm(uint16_t voltage)
    {
        return _setVoltageAlarm(Registers::Alarm_High_Cell_Voltage, voltage);
    }

    bool LC709204F::setLowRSOCAlarm(uint8_t percent)
    {
        if (percent <= 100)
        {
            return _writeReg(Registers::Alarm_Low_RSOC, static_cast<uint16_t>(percent));
        }
        return 0;
    }

    bool LC709204F::setTerminationFactor(float factor)
    {
        if (factor >= LC709204F::MinTerminationFactor && factor <= LC709204F::MaxTerminationFactor)
        {
            return _writeReg(Registers::Termination_Current_Rate, static_cast<uint16_t>(factor * 100));
        }
        return false;
    }

    bool LC709204F::setInitialized()
    {
        uint16_t value = 0;
        if (_readReg(Registers::BatteryStatus, value))
        {
            value &= ~(0b1 << static_cast<uint8_t>(BatteryStatus::Initialized));
            return _writeReg(Registers::BatteryStatus, value);
        }
        return false;
    }

    bool LC709204F::clearLowVoltageAlarm()
    {
        return _clearAlarm(BatteryStatus::LowCellVoltage);
    }

    bool LC709204F::clearHighVoltageAlarm()
    {
        return _clearAlarm(BatteryStatus::HighCellVoltage);
    }

    bool LC709204F::clearLowRSOCAlarm()
    {
        return _clearAlarm(BatteryStatus::LowRSOC);
    }
}