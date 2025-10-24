#pragma once

#include <inttypes.h>
#include <stdlib.h>
#include <driver/gpio.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "my_lcd.h"

#define FIRMWARE_VERSION_STR "portable_angstrem-v1.2.2"

#ifdef __cplusplus

namespace my_hal
{
    typedef uint32_t dac_code_t;

    enum sr_types
    {
        SR_DAC,
        SR_LCD
    };
    enum hardware_rev_types
    {
        pcbV1
    };

    esp_err_t init();

    my_lcd::hd44780_t* get_lcd_config();

    void sr_write(sr_types t, const uint8_t* contents);
    void set_output_enable(bool v);
}

#endif