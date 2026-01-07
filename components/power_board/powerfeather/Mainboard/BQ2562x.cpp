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
#include <climits>

#include <esp_log.h>

#include <Utils/Util.h>
#include "BQ2562x.h"

namespace PowerFeather
{
    static const char *TAG = "PowerFeather::Mainboard::BQ2562x";

    template <typename T>
    bool BQ2562x::_readReg(Register reg, T &value)
    {
        assert(reg.size <= sizeof(value));
        assert(reg.start <= reg.end);
        assert(reg.end <= (reg.size * CHAR_BIT) - 1);

        uint16_t data = 0;
        if (_i2c.read(_i2cAddress, reg.address, reinterpret_cast<uint8_t *>(&data), reg.size))
        {
            int left = (((sizeof(data) * CHAR_BIT) - 1) - reg.end);
            data <<= left;
            data >>= left + reg.start;
            value = data;
            ESP_LOGD(TAG, "Read bit%d to bit%d on %d-byte register %02x, value = %04x.", reg.start, reg.end, reg.size, reg.address, value);
            return true;
        }

        ESP_LOGD(TAG, "Read bit%d to bit%d on %d-byte register %02x failed.", reg.start, reg.end, reg.size, reg.address);
        return false;
    }

    template <typename T>
    bool BQ2562x::_writeReg(Register reg, T value)
    {
        uint8_t last = (reg.size * CHAR_BIT) - 1;

        assert(reg.size <= sizeof(value));
        assert(reg.start <= reg.end);
        assert(reg.end <= last);

        uint16_t data = 0;
        bool isFullRegister = (reg.start == 0 && reg.end == last);

        // Skip read-modify-write for full register writes
        if (isFullRegister)
        {
            data = static_cast<uint16_t>(value);
        }
        else if (!_readReg(Register{reg.address, reg.size, 0, last}, data))
        {
            return false;
        }
        else
        {
            uint8_t bits = reg.end - reg.start + 1;
            uint16_t mask = ((1 << bits) - 1) << reg.start;
            data = (data & ~mask) | ((value << reg.start) & mask);
        }

        bool res = _i2c.write(_i2cAddress, reg.address, reinterpret_cast<uint8_t *>(&data), reg.size);
        if (res)
        {
            ESP_LOGD(TAG, "Write bit%d to bit%d on %d-byte register %02x, value = %04x.", reg.start, reg.end, reg.size, reg.address, data);
        }
        else
        {
            ESP_LOGD(TAG, "Write bit%d to bit%d on %d-byte register %02x failed.", reg.start, reg.end, reg.size, reg.address);
        }
        return res;
    }

    bool BQ2562x::getWD(bool &enabled)
    {
        return _readReg(Charger_Control_0_WATCHDOG, enabled);
    }

    bool BQ2562x::getVBUS(uint16_t &voltage)
    {
        uint16_t value = 0;
        if (_readReg(VBUS_ADC, value))
        {
            voltage = round(Util::fromRaw(value, 3.97f));
            return true;
        }
        return false;
    }

    bool BQ2562x::getIBUS(int16_t &current)
    {
        uint16_t value = 0;

        if (_readReg(IBUS_ADC, value))
        {
            uint16_t origin = (value >= 0x7830 && value <= 0x7fff) ? 0x7fff + 1 : 0;
            current = round(Util::fromRaw(value, 2.0f, origin));
            return true;
        }
        return false;
    }

    bool BQ2562x::getVBAT(uint16_t &voltage)
    {
        uint16_t value = 0;
        if (_readReg(VBAT_ADC, value))
        {
            voltage = round(Util::fromRaw(value, 1.99f));
            return true;
        }
        return false;
    }

    bool BQ2562x::getIBAT(int16_t &current)
    {
        static constexpr uint16_t invalid = 0x2000;
        uint16_t value = 0;
        if (_readReg(IBAT_ADC, value))
        {
            if (value != invalid)
            {
                uint16_t origin = (value >= 0x38ad && value <= 0x3fff) ? 0x3fff + 1 : 0;
                current = round(Util::fromRaw(value, 4.0f, origin));
                return true;
            }
        }
        return false;
    }

    bool BQ2562x::getADCDone(bool &done)
    {
        bool value;
        if (_readReg(Charger_Status_0_ADC_DONE, value))
        {
            done = value;
            return true;
        }
        return false;
    }

    bool BQ2562x::getTSEnabled(bool &enabled)
    {
        bool tsIgnore = false, tsDis = false;
        if (_readReg(NTC_Control_0_TS_IGNORE, tsIgnore))
        {
            Register reg = ADC_Function_Disable_0;
            reg.start = reg.end = static_cast<uint8_t>(Adc::TS);
            if (_readReg(reg, tsDis))
            {
                enabled = (!tsIgnore) && (!tsDis);
                return true;
            }
        }
        return false;
    }

    bool BQ2562x::getTSBias(float &bias)
    {
        uint16_t value = 0;
        if (_readReg(TS_ADC, value))
        {
            bias = Util::fromRaw(value, 0.0961f) / 100.0f;
            return true;
        }
        return false;
    }

    bool BQ2562x::getVBUSStat(VBUSStat &stat)
    {
        uint8_t value = 0;
        if (_readReg(Charger_Status_1_VBUS_STAT, value))
        {
            stat = (value == static_cast<uint8_t>(VBUSStat::Adapter)) ? VBUSStat::Adapter : VBUSStat::None;
            return true;
        }
        return false;
    }

    bool BQ2562x::getChargeStat(ChargeStat &stat)
    {
        uint8_t value = 0;
        if (_readReg(Charger_Status_1_CHG_STAT, value))
        {
            switch (value)
            {
            case static_cast<uint8_t>(ChargeStat::Trickle):
                stat = ChargeStat::Trickle;
                break;
            case static_cast<uint8_t>(ChargeStat::Taper):
                stat = ChargeStat::Taper;
                break;
            case static_cast<uint8_t>(ChargeStat::TopOff):
                stat = ChargeStat::TopOff;
                break;
            case static_cast<uint8_t>(ChargeStat::Terminated):
            default:
                stat = ChargeStat::Terminated;
                break;
            }
            return true;
        }
        return false;
    }

    bool BQ2562x::getChargingEnabled(bool& enabled)
    {
        return _readReg(Charger_Control_0_EN_CHG, enabled);
    }

    bool BQ2562x::getSTATEnabled(bool& enabled)
    {
        if (_readReg(Charge_Timer_Control_DIS_STAT, enabled))
        {
            enabled = !enabled;
            return true;
        }
        return false;
    }

    bool BQ2562x::getVINDPM(uint16_t& voltage)
    {
        uint16_t value = 0;
        if (_readReg(Input_Current_Limit_VINDPM, value))
        {
            voltage = round(Util::fromRaw(value, 40.0f));
            return true;
        }
        return false;
    }

    bool BQ2562x::getChargeCurrentLimit(uint16_t& current)
    {
        uint16_t value = 0;
        if (_readReg(Charge_Current_Limit_ICHG, value))
        {
            current = round(Util::fromRaw(value, 40.0f));
            return true;
        }
        return false;
    }

    bool BQ2562x::getPartInformation(uint8_t &value)
    {
        return _readReg(Part_Information, value);
    }

    bool BQ2562x::setWD(WatchdogTimer timer)
    {
        return _writeReg(Charger_Control_0_WATCHDOG, static_cast<uint8_t>(timer));
    }

    bool BQ2562x::enableCharging(bool enable)
    {
        return _writeReg(Charger_Control_0_EN_CHG, enable);
    }

    bool BQ2562x::enableTS(bool enable)
    {
        return _writeReg(NTC_Control_0_TS_IGNORE, !enable) && enableADC(Adc::TS, enable);
    }

    bool BQ2562x::enableHIZ(bool enable)
    {
        return _writeReg(Charger_Control_0_EN_HIZ, enable);
    }

    bool BQ2562x::enableInterrupts(bool enable)
    {
        uint8_t value = enable ? 0 : 0xFF;
        if (_writeReg(Charger_Mask_0, value))
        {
            if (_writeReg(Charger_Mask_1, value))
            {
                return _writeReg(FAULT_Mask_0, value);
            }
        }
        return false;
    }

    bool BQ2562x::enableInterrupt(Interrupt interrupt, bool enable)
    {
        if (interrupt == Interrupt::VBUS)
        {
            Register reg = Charger_Mask_1;
            reg.start = reg.end = static_cast<uint8_t>(0);
            return _writeReg(reg, !enable);
        }
        else if (interrupt == Interrupt::TS)
        {
            Register reg = FAULT_Mask_0;
            reg.start = reg.end = static_cast<uint8_t>(0);
            return _writeReg(reg, !enable);
        } // supports only a few interrupts for now
        return false;
    }

    bool BQ2562x::enableWVBUS(bool enable)
    {
        return _writeReg(Charger_Control_2_WVBUS, enable);
    }

    bool BQ2562x::enableADC(Adc adc, bool enable)
    {
        Register reg = ADC_Function_Disable_0;
        reg.start = reg.end = static_cast<uint8_t>(adc);
        return _writeReg(reg, !enable);
    }

    bool BQ2562x::enableSTAT(bool enable)
    {
        Register reg = Charge_Timer_Control_DIS_STAT;
        return _writeReg(reg, !enable);
    }

    bool BQ2562x::setChargeCurrentLimit(uint16_t current)
    {
        if (current >= BQ2562x::MinChargingCurrent && current <= BQ2562x::MaxChargingCurrent)
        {
            uint16_t value = round(Util::toRaw(current, 40.0f));
            return _writeReg(Charge_Current_Limit_ICHG, value);
        }
        return false;
    }

    bool BQ2562x::setBATFETControl(BATFETControl control)
    {
        uint8_t value = static_cast<uint8_t>(control);
        return _writeReg(Charger_Control_2_BATFET_CTRL, value);
    }

    bool BQ2562x::setBATFETDelay(BATFETDelay delay)
    {
        uint8_t value = static_cast<uint8_t>(delay);
        return _writeReg(Charger_Control_2_BATFET_DLY, value);
    }

    bool BQ2562x::setVINDPM(uint16_t voltage)
    {
        if (voltage >= BQ2562x::MinVINDPMVoltage && voltage <= BQ2562x::MaxVINDPMVoltage)
        {
            uint16_t value = Util::toRaw(voltage, 40.0f);
            return _writeReg(Input_Current_Limit_VINDPM, value);
        }
        return false;
    }

    bool BQ2562x::setIINDPM(uint16_t current)
    {
        if (current >= BQ2562x::MinIINDPMCurrent && current <= BQ2562x::MaxIINDPMCurrent)
        {
            uint16_t value = Util::toRaw(current, 20.0f);
            return _writeReg(Input_Current_Limit_IINDPM, value);
        }
        return false;
    }

    bool BQ2562x::setITERM(uint16_t current)
    {
        if (current >= BQ2562x::MinITERMCurrent && current <= BQ2562x::MaxITERMCurrent)
        {
            uint16_t value = Util::toRaw(current, 5.0f);
            return _writeReg(Termination_Control_0_ITERM, value);
        }
        return false;
    }

    bool BQ2562x::setTopOff(TopOffTimer timer)
    {
        uint8_t value = static_cast<uint8_t>(timer);
        return _writeReg(Charger_Control_TOPOFF_TMR, value);
    }

    bool BQ2562x::setIbatPk(IbatPkLimit limit)
    {
        uint8_t value = static_cast<uint8_t>(limit);
        return _writeReg(Charger_Control_3_IBAT_PK, value);
    }

    bool BQ2562x::setTH456(TH456Setting setting)
    {
        return _writeReg(NTC_Control_1_TS_TH4_TH5_TH6, static_cast<uint8_t>(setting));
    }

    bool BQ2562x::setTempIset(TempPoint point, TempIset iset)
    {
        Register reg;

        switch (point)
        {
        case TempPoint::Prewarm:
            reg = NTC_Control_2_TS_ISET_PREWARM;
            break;
        case TempPoint::Precool:
            reg = NTC_Control_2_TS_ISET_PRECOOL;
            break;
        case TempPoint::Warm:
            reg = NTC_Control_0_TS_ISET_WARM;
            break;
        case TempPoint::Cool:
            reg = NTC_Control_0_TS_ISET_COOL;
            break;
        default:
            return false;
            break;
        }

        return _writeReg(reg, static_cast<uint8_t>(iset));
    }

    bool BQ2562x::setupADC(bool enable, ADCRate rate, ADCSampling sampling, ADCAverage average, ADCAverageInit averageInit)
    {
        uint8_t value = enable << ADC_Control_ADC_EN.start;
        if (value)
        {
            value |= static_cast<uint8_t>(rate) << ADC_Control_ADC_RATE.start;
            value |= static_cast<uint8_t>(sampling) << ADC_Control_ADC_SAMPLE.start;
            value |= static_cast<uint8_t>(average) << ADC_Control_ADC_AVG.start;
            value |= static_cast<uint8_t>(averageInit) << ADC_Control_ADC_AVG_INIT.start;
        }
        return _writeReg(ADC_Control, value);
    }
}
