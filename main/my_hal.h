#pragma once

#include <inttypes.h>
#include <stdlib.h>
#include <driver/gpio.h>
#include <esp_err.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <esp_netif.h>

#include "my_lcd.h"

#define FIRMWARE_VERSION_STR "cpwr-v0.2"

#define MY_PWR_MAX 3.0f
#define MY_VLIM_MAX 5.5f
#define MY_VLIM_MIN 1.3f

#define ENCODER_RESOLUTION_STEP 0.001f //W

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
    int64_t get_encoder_counts();
    esp_netif_t* get_netif();
    bool get_btn_pressed();

    void reset_encoder();
    void sr_write(sr_types t, const uint8_t* contents);
    void set_output_enable(bool v);
}

#endif