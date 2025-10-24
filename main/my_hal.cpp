/**
 * @file my_hal.cpp
 * @author MSU
 * @brief Hardware Abstraction Layer: GPIO pin layout, hardware timers and PWM (buzzer, LED and analog PSU soft-start),
 * ADC channels, entering and recovering from sleep mode, Shift-Register IO, CPU power management
 * @date 2022-10-21
 * 
 */

#include "my_hal.h"

#include "params.h"
#include "macros.h"
#include "my_dac.h"

#include "esp_log.h"
#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include <rom/ets_sys.h>
#include <driver/ledc.h>
#include <soc/rtc.h>
#include <soc/rtc_cntl_reg.h>
#include <driver/rtc_io.h>
#include <rom/gpio.h>

#define MAX_CPU_FREQ_MHZ 160
#define DEFAULT_CPU_FREQ_MHZ 80
#define MIN_CPU_FREQ_MHZ 40

static const char TAG[] = "HAL";

esp_err_t lcd_write_callback(const my_lcd::hd44780_t* lcd, uint8_t b);

/**
 * Configuration Section: Pin Numbers
 * 
 */

const gpio_num_t pin_btn = GPIO_NUM_35;
const gpio_num_t pin_oe = GPIO_NUM_14;
const gpio_num_t pin_lcd_rs = GPIO_NUM_32;
const gpio_num_t pin_lcd_e = GPIO_NUM_33;
const gpio_num_t pin_enc_a = GPIO_NUM_39;
const gpio_num_t pin_enc_b = GPIO_NUM_36;

const gpio_num_t input_gpio[] =
{
    pin_btn,
    pin_enc_a,
    pin_enc_b
};
const gpio_num_t output_gpio[] =
{
    pin_oe,
    pin_lcd_rs,
    pin_lcd_e
};

/**
 * @brief Shift Registers
 * 
 */
struct my_sr
{
    gpio_num_t d;
    gpio_num_t clk;
    gpio_num_t latch;
    bool msb_first; //Bit order
    size_t len; //Bytes
};

const my_sr regs[] = 
{
    { GPIO_NUM_12, GPIO_NUM_2, GPIO_NUM_4, true, 3 }, // DACs
    { GPIO_NUM_12, GPIO_NUM_15, GPIO_NUM_4, true, 1 } // LCD
};

/// @brief LCD configuration for hd44780 library. Note that the databus is handled externally, because it's driven by a 595 shift register.
static my_lcd::hd44780_t lcd_cfg = 
{
    lcd_write_callback, //!< Data write callback. Set it to NULL in case of direct LCD connection to GPIO
    {
        pin_lcd_rs,        //!< GPIO/register bit used for RS pin
        pin_lcd_e,         //!< GPIO/register bit used for E pin
        GPIO_NUM_NC,
        GPIO_NUM_NC,
        GPIO_NUM_NC,
        GPIO_NUM_NC,
        GPIO_NUM_NC,        //!< GPIO/register bit used for D4 pin
        GPIO_NUM_NC,        //!< GPIO/register bit used for D5 pin
        GPIO_NUM_NC,        //!< GPIO/register bit used for D5 pin
        GPIO_NUM_NC,        //!< GPIO/register bit used for D5 pin
        static_cast<gpio_num_t>(HD44780_NOT_USED)        //!< GPIO/register bit used for backlight. Set it `HD44780_NOT_USED` if no backlight used
    },
    my_lcd::hd44780_font_t::HD44780_FONT_5X8,   //!< LCD Font type
    2,         //!< Number of lines for LCD.
    true        //!< Current backlight state
};

/**
 * PUBLIC routines
 * 
 */

namespace my_hal
{
    /// @brief Initialize HAL and the peripherals it contorls.
    /// @param buzzer_freq Buzzer frequency (driving square wave frequency, harmonics will be present)
    /// @param shdn_pump_freq Driving frequency for the charge pump that drives the gate of the MOSFET that enables analog PSU
    /// @return ESK_OK, or panics otherwise
    esp_err_t init()
    {
        const uint32_t zero = 0;
        const uint8_t* const zero_ptr = reinterpret_cast<const uint8_t*>(&zero);
        ESP_LOGI(TAG, "HAL initialization");

        ESP_LOGI(TAG, "Init GPIO direction...");
        //Output pins
        for (auto &&i : output_gpio)
        {
            gpio_pad_select_gpio(i);
            ESP_ERROR_CHECK(gpio_set_direction(i, GPIO_MODE_OUTPUT));
            ESP_ERROR_CHECK(gpio_set_level(i, i == pin_oe ? 1 : 0));
        }
        for (auto &&i : input_gpio)
        {
            gpio_pad_select_gpio(i);
            ESP_ERROR_CHECK(gpio_set_direction(i, GPIO_MODE_INPUT));
            ESP_ERROR_CHECK(gpio_set_pull_mode(i, GPIO_FLOATING));
        }

        ESP_LOGI(TAG, "Init SRs...");
        //Set shift register pins as outputs and load all zeros
        for (size_t i = 0; i < ARRAY_SIZE(regs); i++)
        {
            gpio_set_direction(regs[i].d, GPIO_MODE_OUTPUT);
            gpio_set_direction(regs[i].clk, GPIO_MODE_OUTPUT);
            gpio_set_direction(regs[i].latch, GPIO_MODE_OUTPUT);
            assert(regs[i].len <= sizeof(zero));
            sr_write(static_cast<sr_types>(i), zero_ptr);
        }
        set_output_enable(true);
        
        ESP_LOGI(TAG, "HAL init finished");
        return ESP_OK;
    }

    /// @brief Get HD44780 LCD library configuration
    /// @return Configuration structure
    my_lcd::hd44780_t* get_lcd_config()
    {
        return &lcd_cfg;
    }
    /// @brief Write bytes to a shift register chain
    /// @param t Shift register chain
    /// @param contents Buffer to write from
    void sr_write(sr_types t, const uint8_t* contents)
    {
        const size_t byte_len = 8;
        assert(t < ARRAY_SIZE(regs));
        static_assert(sizeof(dac_code_t) >= 3, "Warning: check DAC shift register length!");

        auto sr = regs[t];
        ESP_ERROR_CHECK(gpio_set_level(sr.latch, 0));
        for (size_t i = 0; i < sr.len; i++)
        {
            for (size_t j = 0; j < byte_len; j++)
            {
                uint32_t mask = 1u << (sr.msb_first ? (byte_len - 1 - j) : j);
                ESP_ERROR_CHECK(gpio_set_level(sr.clk, 0));
                ESP_ERROR_CHECK(gpio_set_level(sr.d, (contents[sr.msb_first ? (sr.len - 1 - i) : i] & mask) > 0));
                ets_delay_us(1);
                ESP_ERROR_CHECK(gpio_set_level(sr.clk, 1));
                ets_delay_us(1);
            }
        }
        ESP_ERROR_CHECK(gpio_set_level(sr.latch, 1));
    }
    /// @brief 
    /// @return True == the button is pressed, false otherwise
    bool get_btn_pressed()
    {
        return gpio_get_level(pin_btn) == 0; //Active LOW
    }
    /// @brief Enable DAC outputs. They should be disabled when analog PSU is not active for power not to leak into analog circuits.
    /// @param v True == enable
    void set_output_enable(bool v)
    {
        ESP_ERROR_CHECK(gpio_set_level(pin_oe, !v)); //Active Low
    }
}

/// @brief Hardware interface implementation for HD4470 LCD library. 
/// @param lcd LCD handle
/// @param b Byte to write to the data bus
/// @return ESP_OK (always)
esp_err_t lcd_write_callback(const my_lcd::hd44780_t* lcd, uint8_t b)
{
    my_hal::sr_write(my_hal::sr_types::SR_LCD, &b);

    return ESP_OK;
}