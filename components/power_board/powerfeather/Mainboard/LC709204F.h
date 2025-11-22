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

#pragma once

#include <tuple>

#include "Utils/MasterI2C.h"

namespace PowerFeather
{
    class LC709204F
    {
    public:

        static constexpr uint16_t MinVoltageAlarm = 2500;
        static constexpr uint16_t MaxVoltageAlarm = 5000;
        static constexpr uint16_t MaxBatteryCapacity = 6000;
        static constexpr uint16_t MinBatteryCapacity = 50;
        static constexpr float MaxTerminationFactor = 0.3f;
        static constexpr float MinTerminationFactor = 0.02f;

        static constexpr float MinTemperature = -30.0f;
        static constexpr float MaxTemperature = 80.0f;

        static constexpr uint16_t MinTemperatureRaw = 0x0980;
        static constexpr uint16_t MaxTemperatureRaw = 0x0DCC;

        enum class Registers
        {
            TimeToEmpty = 0x03,
            TimeToFull = 0x05,
            TSENSE1 = 0x08,
            Cell_Voltage = 0x09,
            APA = 0x0b,
            RSOC = 0x0d,
            Change_Of_The_Parameter = 0x12,
            Alarm_Low_RSOC = 0x13,
            Alarm_Low_Cell_Voltage = 0x14,
            IC_Power_Mode = 0x15,
            Status_Bit = 0x16,
            Cycle_Count = 0x17,
            BatteryStatus = 0x19,
            Termination_Current_Rate = 0x1c,
            Alarm_High_Cell_Voltage = 0x1f,
            State_Of_Health = 0x32
        };

        enum class ChangeOfParameter
        {
            Nominal_3V7_Charging_4V2 = 0x00,
            UR18650ZY = 0x01,
            ICR18650_26H = 0x02,
            Nominal_3V8_Charging_4V35 = 0x03,
            Nominal_3V85_Charging_4V4 = 0x04,
        };

        LC709204F(MasterI2C &i2c) : _i2c(i2c) {}

        bool getOperationMode(bool &enabled);
        bool getCellVoltage(uint16_t &voltage);
        bool getRSOC(uint8_t &percent);
        bool getTimeToEmpty(uint16_t &minutes);
        bool getTimeToFull(uint16_t &minutes);
        bool getCellTemperature(float &temperature);
        bool getCycles(uint16_t &cycles);
        bool getSOH(uint8_t &percent);
        bool getInitialized(bool& state);
        bool setOperationMode(bool enable);
        bool setAPA(uint16_t capacity, ChangeOfParameter changeOfParam);
        bool setChangeOfParameter(ChangeOfParameter changeOfParam);
        bool setCellTemperature(float temperature);
        bool enableTSENSE(bool enableTsense1, bool enableTsense2);
        bool setLowVoltageAlarm(uint16_t voltage);
        bool setHighVoltageAlarm(uint16_t voltage);
        bool setLowRSOCAlarm(uint8_t percent);
        bool setTerminationFactor(float factor);
        bool setInitialized();
        bool clearLowVoltageAlarm();
        bool clearHighVoltageAlarm();
        bool clearLowRSOCAlarm();

    private:
        enum class BatteryStatus : uint8_t
        {
            LowRSOC=9,
            LowCellVoltage=11,
            HighCellVoltage=15,
            Initialized=7
        };

        enum class OperationMode : uint16_t
        {
            OperationalMode = 0x0001,
            SleepMode = 0x0002
        };

        const std::tuple<uint16_t, uint8_t> _apaTable[10] =
        {
            {50, 0x13},
            {100, 0x15},
            {200, 0x18},
            {500, 0x21},
            {1000, 0x2D},
            {2000, 0x3A},
            {3000, 0x3F},
            {4000, 0x42},
            {5000, 0x44},
            {6000, 0x45},
        };

        static constexpr uint8_t _i2cAddress = 0x0b;

        MasterI2C &_i2c;

        bool _readReg(Registers reg, uint16_t &data);
        bool _writeReg(Registers reg, uint16_t data);
        uint8_t _computeCRC8(uint8_t *data, int len);

        bool _setVoltageAlarm(Registers reg, uint16_t voltage);
        bool _clearAlarm(BatteryStatus alarm);
    };
}