#pragma once

#include "my_lcd.h"
#include "esp_err.h"

namespace menu
{
    enum localized_messages
    {
        initializing,

        TOTAL_MESSAGES
    };

    esp_err_t init(my_lcd::hd44780_t* lcd);

    bool set_values(float watts, float vlim);

    void repaint();
    void print_str(const char* s);
    void print_message(localized_messages m);
    void print_message_f(localized_messages m, ...);
} // namespace my_menu
