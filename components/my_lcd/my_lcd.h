/*
 * Copyright (c) 2016 Ruslan V. Uss <unclerus@gmail.com>
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
 * @file hd44780.h
 * @defgroup hd44780 hd44780
 * @{
 *
 * ESP-IDF driver for HD44780 compatible LCD text displays
 *
 * Ported from esp-open-rtos
 *
 * Copyright (c) 2016 Ruslan V. Uss <unclerus@gmail.com>
 *
 * BSD Licensed as described in the file LICENSE
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <driver/gpio.h>
#include <esp_err.h>

#define HD44780_NOT_USED 0xff
#define MY_LCD_8_BIT 1 //1 == 8 bit bus, 0 == 4 bit bus

#ifndef _BV
    #define _BV(i) (1u << (i))
#endif

namespace my_lcd
{
    /**
     * LCD font type. Please refer to the datasheet
     * of your module.
     */
    enum hd44780_font_t
    {
        HD44780_FONT_5X8 = 0,
        HD44780_FONT_5X10
    };
    enum an6866_page_t
    {
        AN6866_PAGE_0 = 0,
        AN6866_PAGE_1 = _BV(1)
    };

    struct hd44780_t;

    typedef esp_err_t (*hd44780_write_cb_t)(const hd44780_t *lcd, uint8_t data);

    /**
     * LCD descriptor. Fill it before use.
     */
    struct hd44780_t
    {
        hd44780_write_cb_t write_cb; //!< Data write callback. Set it to NULL in case of direct LCD connection to GPIO
        struct
        {
            gpio_num_t rs;        //!< GPIO/register bit used for RS pin
            gpio_num_t e;         //!< GPIO/register bit used for E pin
#if MY_LCD_8_BIT
            gpio_num_t d0;
            gpio_num_t d1;
            gpio_num_t d2;
            gpio_num_t d3;
#endif
            gpio_num_t d4;        //!< GPIO/register bit used for D4 pin
            gpio_num_t d5;        //!< GPIO/register bit used for D5 pin
            gpio_num_t d6;        //!< GPIO/register bit used for D5 pin
            gpio_num_t d7;        //!< GPIO/register bit used for D5 pin
            gpio_num_t bl;        //!< GPIO/register bit used for backlight. Set it `HD44780_NOT_USED` if no backlight used
        } pins;
        hd44780_font_t font;   //!< LCD Font type
        uint8_t lines;         //!< Number of lines for LCD. Many 16x1 LCD has two lines (like 8x2)
        bool backlight;        //!< Current backlight state
    };

    esp_err_t set_function(const hd44780_t *lcd, an6866_page_t ch_page = an6866_page_t::AN6866_PAGE_0);

    /**
     * @brief Init LCD
     *
     * Set cursor position to (0, 0)
     *
     * @param lcd LCD descriptor
     * @return `ESP_OK` on success
     */
    esp_err_t init(const hd44780_t *lcd, an6866_page_t ch_page = an6866_page_t::AN6866_PAGE_0);

    /**
     * @brief Control LCD
     *
     * On/off LCD, show/hide cursor, set cursor blink
     *
     * @param lcd LCD descriptor
     * @param on Switch LCD on if true
     * @param cursor Show cursor if true
     * @param cursor_blink Enable cursor blinking if true
     * @return `ESP_OK` on success
     */
    esp_err_t control(const hd44780_t *lcd, bool on, bool cursor, bool cursor_blink);

    /**
     * @brief Clear LCD
     *
     * Clear memory and move cursor to (0, 0)
     *
     * @param lcd LCD descriptor
     * @return `ESP_OK` on success
     */
    esp_err_t clear(const hd44780_t *lcd);

    /**
     * @brief Move cursor
     *
     * @param lcd LCD descriptor
     * @param col Column
     * @param line Line
     * @return `ESP_OK` on success
     */
    esp_err_t gotoxy(const hd44780_t *lcd, uint8_t col, uint8_t line);

    /**
     * @brief Write character at cursor position
     *
     * @param lcd LCD descriptor
     * @param c Character to write
     * @return `ESP_OK` on success
     */
    esp_err_t putc(const hd44780_t *lcd, char c, char cp_offset = 0);

    /**
     * @brief Write NULL-terminated string at cursor position
     *
     * @param lcd LCD descriptor
     * @param s String to write
     * @return `ESP_OK` on success
     */
    esp_err_t puts(const hd44780_t *lcd, const char *s, char cp_offset = 0);

    /**
     * @brief Switch backlight
     *
     * @param lcd LCD descriptor
     * @param on Turn backlight on if true
     * @return `ESP_OK` on success
     */
    esp_err_t switch_backlight(hd44780_t *lcd, bool on);

    /**
     * @brief Upload character data to the CGRAM
     *
     * After upload cursor will be moved to (0, 0).
     *
     * @param lcd LCD descriptor
     * @param num Character number (0..7)
     * @param data Character data: 8 or 10 bytes depending on the font
     * @return `ESP_OK` on success
     */
    esp_err_t upload_character(const hd44780_t *lcd, uint8_t num, const uint8_t *data);

    /**
     * @brief Scroll the display content to left by one character
     *
     * @param lcd LCD descriptor
     * @return `ESP_OK` on success
     */
    esp_err_t scroll_left(const hd44780_t *lcd);

    /**
     * @brief Scroll the display content to right by one character
     *
     * @param lcd LCD descriptor
     * @return `ESP_OK` on success
     */
    esp_err_t scroll_right(const hd44780_t *lcd);

} // namespace my_lcd

/**
 * @}
*/