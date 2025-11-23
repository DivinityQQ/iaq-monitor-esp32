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

#include "Mutex.h"

namespace PowerFeather
{
    static const char *TAG = "PowerFeather::Utils::Mutex";

    void Mutex::init()
    {
        _sem = xSemaphoreCreateRecursiveMutex();
        ESP_LOGD(TAG, "Mutex %p created.", this);
    }

    bool Mutex::lock()
    {
        if (_sem == nullptr)
        {
            ESP_LOGD(TAG, "Mutex %p not initialized.", this);
            return false;
        }

        if (xSemaphoreTakeRecursive(_sem, pdMS_TO_TICKS(_timeout)) == pdTRUE)
        {
            ESP_LOGD(TAG, "Mutex %p take succeeded.", this);
            return true;
        }
        ESP_LOGD(TAG, "Failed to take mutex %p.", this);
        return false;
    }

    void Mutex::unlock()
    {
        if (_sem)
        {
            xSemaphoreGiveRecursive(_sem);
            ESP_LOGD(TAG, "Mutex %p released.", this);
        }
    }
}
