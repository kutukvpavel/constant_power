#include "my_lcd.h"

/*
 * Copyright (c) 2016 Ruslan V. Uss <unclerus@gmail.com>
 *
 * Adapted for MELT (melt.com.ru) displays that use AN6866 compatible controllers
 * Those include second ROM-"codepage" and other extensions
 * 
 * Paul Kutukov, 2022
 * 
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 * 3. Neither the name of the copyright holder nor the names of itscontributors
 *    may be used to endorse or promote products derived from this software without
 *    specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/**
 * @file hd44780.c
 *
 * ESP-IDF driver for HD44780 compatible LCD text displays
 *
 * Ported from esp-open-rtos
 *
 * Copyright (c) 2016 Ruslan V. Uss <unclerus@gmail.com>
 *
 * BSD Licensed as described in the file LICENSE
 */
#include <string.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_system.h>
#include <rom/ets_sys.h>
#include "my_lcd.h"
#include "macros.h"

#define MS 1000

#define GPIO_BIT(x) (1ULL << (x))

#define DELAY_CMD_LONG  (3000) // >1.53ms according to datasheet
#define DELAY_CMD_SHORT (40)     // >39us according to datasheet
#define DELAY_TOGGLE    (1)      // E cycle time >= 1μs, E pulse width >= 450ns, Data set-up time >= 195ns
#define DELAY_INIT      (5 * MS)

#define CMD_CLEAR        0x01
#define CMD_RETURN_HOME  0x02
#define CMD_ENTRY_MODE   0x04
#define CMD_DISPLAY_CTRL 0x08
#define CMD_SHIFT        0x10
#define CMD_FUNC_SET     0x20
#define CMD_CGRAM_ADDR   0x40
#define CMD_DDRAM_ADDR   0x80

#define ARG_MOVE_RIGHT 0x04
#define ARG_MOVE_LEFT 0x00
#define CMD_SHIFT_LEFT  (CMD_SHIFT | CMD_DISPLAY_CTRL | ARG_MOVE_LEFT)
#define CMD_SHIFT_RIGHT (CMD_SHIFT | CMD_DISPLAY_CTRL | ARG_MOVE_RIGHT)

// CMD_ENTRY_MODE
#define ARG_EM_INCREMENT    _BV(1)
#define ARG_EM_SHIFT        (1)

// CMD_DISPLAY_CTRL
#define ARG_DC_DISPLAY_ON   _BV(2)
#define ARG_DC_CURSOR_ON    _BV(1)
#define ARG_DC_CURSOR_BLINK (1)

// CMD_FUNC_SET
#define ARG_FS_8_BIT        _BV(4)
#define ARG_FS_2_LINES      _BV(3)
#define ARG_FS_FONT_5X10    _BV(2)
#define ARG_FS_4_LINES      _BV(4)

#define init_delay()   do { ets_delay_us(DELAY_INIT); } while (0)
#define short_delay()  do { ets_delay_us(DELAY_CMD_SHORT); } while (0)
#define long_delay()   do { vTaskDelay(pdMS_TO_TICKS(DELAY_CMD_LONG / 1000)); } while (0)
#define toggle_delay() do { ets_delay_us(DELAY_TOGGLE); } while (0)

#define CHECK_ARG(VAL) do { if (!(VAL)) return ESP_ERR_INVALID_ARG; } while (0)
#define CHECK(x) do { esp_err_t __; if ((__ = x) != ESP_OK) return __; } while (0)

static const uint8_t line_addr[] = { 0x00, 0x40, 0x14, 0x54 };

#if !MY_LCD_8_BIT
static esp_err_t write_nibble(const my_lcd::hd44780_t *lcd, uint8_t b, bool rs)
{
    if (lcd->write_cb)
    {
        uint8_t data = (((b >> 3) & 1) << lcd->pins.d7)
                     | (((b >> 2) & 1) << lcd->pins.d6)
                     | (((b >> 1) & 1) << lcd->pins.d5)
                     | ((b & 1) << lcd->pins.d4)
                     | (rs ? 1 << lcd->pins.rs : 0)
                     | (lcd->backlight ? 1 << lcd->pins.bl : 0);
        CHECK(lcd->write_cb(lcd, data | (1 << lcd->pins.e)));
        toggle_delay();
        CHECK(lcd->write_cb(lcd, data));
    }
    else
    {
        CHECK(gpio_set_level(lcd->pins.rs, rs));
        ets_delay_us(1); // Address Setup time >= 60ns.
        CHECK(gpio_set_level(lcd->pins.e, true));
        CHECK(gpio_set_level(lcd->pins.d7, (b >> 3) & 1));
        CHECK(gpio_set_level(lcd->pins.d6, (b >> 2) & 1));
        CHECK(gpio_set_level(lcd->pins.d5, (b >> 1) & 1));
        CHECK(gpio_set_level(lcd->pins.d4, b & 1));
        toggle_delay();
        CHECK(gpio_set_level(lcd->pins.e, false));
    }

    return ESP_OK;
}
#endif

static esp_err_t write_byte(const my_lcd::hd44780_t *lcd, uint8_t b, bool rs)
{
#if MY_LCD_8_BIT
    gpio_set_level(lcd->pins.rs, rs);
    ets_delay_us(1); // Address Setup time >= 60ns.
    gpio_set_level(lcd->pins.e, true);
    if (lcd->write_cb)
    {
        lcd->write_cb(lcd, b);
    }
    else
    {
        CHECK(gpio_set_level(lcd->pins.d7, (b >> 7) & 1));
        CHECK(gpio_set_level(lcd->pins.d6, (b >> 6) & 1));
        CHECK(gpio_set_level(lcd->pins.d5, (b >> 5) & 1));
        CHECK(gpio_set_level(lcd->pins.d4, (b >> 4) & 1));
        CHECK(gpio_set_level(lcd->pins.d3, (b >> 3) & 1));
        CHECK(gpio_set_level(lcd->pins.d2, (b >> 2) & 1));
        CHECK(gpio_set_level(lcd->pins.d1, (b >> 1) & 1));
        CHECK(gpio_set_level(lcd->pins.d0, b & 1));
    }
    toggle_delay();
    CHECK(gpio_set_level(lcd->pins.e, false));
#else
    CHECK(write_nibble(lcd, b >> 4, rs));
    CHECK(write_nibble(lcd, b, rs));
#endif

    return ESP_OK;
}

namespace my_lcd
{
    esp_err_t init(const hd44780_t *lcd, an6866_page_t page)
    {
        CHECK_ARG(lcd && lcd->lines > 0 && lcd->lines < 5);

        if (!lcd->write_cb)
        {
            gpio_config_t io_conf;
            memset(&io_conf, 0, sizeof(gpio_config_t));
            io_conf.mode = GPIO_MODE_OUTPUT;
            io_conf.pin_bit_mask =
                    GPIO_BIT(lcd->pins.rs) |
                    GPIO_BIT(lcd->pins.e) |
                    GPIO_BIT(lcd->pins.d4) |
                    GPIO_BIT(lcd->pins.d5) |
                    GPIO_BIT(lcd->pins.d6) |
                    GPIO_BIT(lcd->pins.d7);
            if (lcd->pins.bl != HD44780_NOT_USED)
                io_conf.pin_bit_mask |= GPIO_BIT(lcd->pins.bl);
            CHECK(gpio_config(&io_conf));
        }

        // switch to 4 bit mode
#if MY_LCD_8_BIT
        for (size_t i = 0; i < 3; i++)
        {
            write_byte(lcd, ARG_FS_8_BIT | CMD_FUNC_SET, false);
            init_delay();
        }
#else
        for (uint8_t i = 0; i < 3; i ++)
        {
            CHECK(write_nibble(lcd, (CMD_FUNC_SET | ARG_FS_8_BIT) >> 4, false));
            init_delay();
        }
        CHECK(write_nibble(lcd, CMD_FUNC_SET >> 4, false));
#endif
        short_delay();

        // Specify the number of display lines and character font
        CHECK(set_function(lcd, page));
        // Display off
        CHECK(control(lcd, false, false, false));
        // Clear
        CHECK(clear(lcd));
        // Entry mode set
        CHECK(write_byte(lcd, CMD_ENTRY_MODE | ARG_EM_INCREMENT, false));
        short_delay();
        // Display on
        CHECK(control(lcd, true, false, false));

        return ESP_OK;
    }

    esp_err_t set_function(const hd44780_t *lcd, an6866_page_t ch_page)
    {
        CHECK(write_byte(lcd,
                CMD_FUNC_SET
                    | (lcd->lines > 1 ? ARG_FS_2_LINES : 0)
                    | (lcd->lines > 2 ? ARG_FS_4_LINES : 0)
                    | (lcd->font == HD44780_FONT_5X10 ? ARG_FS_FONT_5X10 : 0)
                    | ch_page,
                false));
        short_delay();

        return ESP_OK;
    }

    esp_err_t control(const hd44780_t *lcd, bool on, bool cursor, bool cursor_blink)
    {
        CHECK_ARG(lcd);

        CHECK(write_byte(lcd,
            CMD_DISPLAY_CTRL
                | (on ? ARG_DC_DISPLAY_ON : 0)
                | (cursor ? ARG_DC_CURSOR_ON : 0)
                | (cursor_blink ? ARG_DC_CURSOR_BLINK : 0),
            false));
        short_delay();

        return ESP_OK;
    }

    esp_err_t clear(const hd44780_t *lcd)
    {
        CHECK_ARG(lcd);

        CHECK(write_byte(lcd, CMD_CLEAR, false));
        long_delay();

        return ESP_OK;
    }

    esp_err_t gotoxy(const hd44780_t *lcd, uint8_t col, uint8_t line)
    {
        CHECK_ARG(lcd && line < lcd->lines && line < sizeof(line_addr));

        CHECK(write_byte(lcd, CMD_DDRAM_ADDR + line_addr[line] + col, false));
        short_delay();

        return ESP_OK;
    }

    esp_err_t putc(const hd44780_t *lcd, char c, char cp_offset)
    {
        CHECK_ARG(lcd);

        CHECK(write_byte(lcd, c + cp_offset, true));
        short_delay();

        return ESP_OK;
    }

    esp_err_t puts(const hd44780_t *lcd, const char *s, char cp_offset)
    {
        CHECK_ARG(lcd && s);

        while (*s)
        {
            CHECK(putc(lcd, *s, cp_offset));
            s++;
        }

        return ESP_OK;
    }

    esp_err_t switch_backlight(hd44780_t *lcd, bool on)
    {
        CHECK_ARG(lcd);
        if (lcd->pins.bl == HD44780_NOT_USED)
            return ESP_ERR_NOT_SUPPORTED;

        if (!lcd->write_cb)
            CHECK(gpio_set_level(lcd->pins.bl, on));
        else
            CHECK(lcd->write_cb(lcd, on ? _BV(lcd->pins.bl) : 0));

        lcd->backlight = on;

        return ESP_OK;
    }

    esp_err_t upload_character(const hd44780_t *lcd, uint8_t num, const uint8_t *data)
    {
        CHECK_ARG(lcd && data && num < 8);

        uint8_t bytes = lcd->font == HD44780_FONT_5X8 ? 8 : 10;
        CHECK(write_byte(lcd, CMD_CGRAM_ADDR + num * bytes, false));
        short_delay();
        for (uint8_t i = 0; i < bytes; i ++)
        {
            CHECK(write_byte(lcd, data[i], true));
            short_delay();
        }

        CHECK(gotoxy(lcd, 0, 0));

        return ESP_OK;
    }

    esp_err_t scroll_left(const hd44780_t *lcd)
    {
        CHECK_ARG(lcd);

        CHECK(write_byte(lcd, CMD_SHIFT_LEFT, false));
        short_delay();

        return ESP_OK;
    }

    esp_err_t scroll_right(const hd44780_t *lcd)
    {
        CHECK_ARG(lcd);

        CHECK(write_byte(lcd, CMD_SHIFT_RIGHT, false));
        short_delay();

        return ESP_OK;
    }
} // namespace my_lcd