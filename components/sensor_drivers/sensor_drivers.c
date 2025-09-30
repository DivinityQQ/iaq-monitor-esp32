/* components/sensor_drivers/sensor_drivers.c */
#include "sensor_drivers.h"
#include "s8_driver.h"

esp_err_t s8_calibrate_co2(int ppm)
{
    (void)ppm;
    // Stub: real implementation will talk to Senseair S8 over UART
    return ESP_ERR_NOT_SUPPORTED;
}
