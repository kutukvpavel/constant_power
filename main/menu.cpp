/**
 * @file menu.cpp
 * @author MSU
 * @brief This unit provides alpha-numeric front panel display abstraction (different menu layouts for actions, localization).
 * LCD repaint is preformed asynchronously in a separate task, accoring to the values contained in RAM "cache",
 * that is manipulated by public API presented by display namespace
 * @date 2024-11-27
 * 
 */

#include "menu.h"

#include "macros.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"

#include <inttypes.h>
#include <math.h>
#include <string.h>

/** Display right column offset (left column offset is 0), in screen coordinate convention */
#define MY_DISPLAY_WIDTH 8u
#define MY_DISPLAY_HEIGHT 2u
#define MY_MENU_COLUMN_OFFSET (MY_DISPLAY_WIDTH - 2)
/** Calculates right column width based on its offset and display width */
#define MY_MENU_RIGHT_COLUMN_WIDTH (MY_DISPLAY_WIDTH - MY_MENU_COLUMN_OFFSET)

//Localization
/** Russian alphabet LCD ROM offset */
#define MY_ALPHABET_ROM_OFFSET 0xC0
/** Get Russian character code for LCD from a wide char @hideinitializer */
#define R(wch) static_cast<char>(static_cast<uint16_t>(wch) - static_cast<uint16_t>(L'А') + MY_ALPHABET_ROM_OFFSET)

/**
 * Right column
 */
const char txt_units[MY_MENU_RIGHT_COLUMN_WIDTH] = "W";
const char txt_units_vlim[MY_MENU_RIGHT_COLUMN_WIDTH] = "V";

/**
 * Messages (displayed on the whole screen, no grid, usually during boot)
 */
/// @brief Text to be displayed during device initialization (during boot)
/// @hideinitializer
const char boot_initializing[] = 
    { 
        R(L'И'), R(L'н'), R(L'и'), R(L'ц'), R(L'и'), R(L'а'), R(L'л'), R(L'и'), R(L'з'), R(L'а'), R(L'ц'), R(L'и'), R(L'я'), 
        '.', '.', '.', '\0' 
    };
/// @brief Localized message array
const char* const txt_messages[] = { 
    boot_initializing
};

/** Tries to acquire LCD repaint mutex with a timeout of 1 second */
#define ACQUIRE_REPAINT_MUTEX() BaseType_t xResult = xSemaphoreTake(repaint_mutex, pdMS_TO_TICKS(1000))
/** Releases previously acquired LCD repaint mutex, or report a warning into the debug console if the code was unable to acquire the mutex beforehand */
#define RELEASE_REPAINT_MUTEX() if (xResult == pdTRUE) xSemaphoreGive(repaint_mutex); \
        else { ESP_LOGW(TAG, "Failed to acquire LCD repaint mutex! Scheduling another repaint..."); repaint(); }

/// @brief Position in screen coordinates (x-axis goes from top to bottom, y-axis goes from left to right)
struct position_t
{
    uint32_t x;
    uint32_t y;
};

/// @brief Pointer to the LCD library configuration structure, set by init function
static my_lcd::hd44780_t* lcd_cfg;

static void repaint_task_body(void* pvParameter);

/// @brief Debug log tag
static const char TAG[] = "LCD_MENU";

namespace menu
{
    /// @brief LCD repaint task handle, is created by init function
    static TaskHandle_t repaint_task_handle = NULL;
    /// @brief LCD repaint mutex, is created by init function
    static SemaphoreHandle_t repaint_mutex = NULL;

    /// @brief Part of the LCD RAM "cache": hydrogen concentration string
    static char watts_buffer[MY_MENU_COLUMN_OFFSET + 1];
    static char vlim_buffer[MY_MENU_COLUMN_OFFSET + 1];
    static bool have_to_clear = true;

    /// @brief Initialize LCD library, create FreeRTOS primitives, start LCD repaint task
    /// @param lcd Pointer to HD44780 library configuration structure
    /// @return see my_lcd::init
    esp_err_t init(my_lcd::hd44780_t* lcd)
    {
        assert(lcd);
        lcd_cfg = lcd;

        auto ret = my_lcd::init(lcd_cfg, my_lcd::an6866_page_t::AN6866_PAGE_0);
        repaint_mutex = xSemaphoreCreateMutex();
        assert(repaint_mutex);
        xTaskCreate(repaint_task_body, "MY_MENU_task", 3072, NULL, 1, &repaint_task_handle);
        assert(repaint_task_handle);
        return ret;
    }
    /// @brief Clear LCD and print a raw string starting at origin. Performs the operation immediately.
    /// @param s String
    void print_str(const char* s)
    {
        my_lcd::clear(lcd_cfg);
        my_lcd::puts(lcd_cfg, s);
        have_to_clear = true;
    }
    /// @brief 
    /// @param watts 
    /// @param vlim 
    /// @return True == need repaint
    bool set_values(float watts, float vlim)
    {
        const char blank[] = "-----";
        static_assert(ARRAY_SIZE(blank) == (MY_MENU_COLUMN_OFFSET));
        static float prev_w = NAN;
        static float prev_vlim = NAN;

        ACQUIRE_REPAINT_MUTEX();

        bool need_repaint = (prev_w != watts) || (prev_vlim != vlim);
        if (isfinite(watts))
        {
            snprintf(watts_buffer, ARRAY_SIZE(watts_buffer), "%1.3f", watts);
        }
        else    
        {
            strncpy(watts_buffer, blank, ARRAY_SIZE(watts_buffer));
        }
        if (isfinite(vlim))
        {
            snprintf(vlim_buffer, ARRAY_SIZE(vlim_buffer), "%1.1f", vlim);
        }
        else
        {
            strncpy(vlim_buffer, blank, ARRAY_SIZE(vlim_buffer));
        }
        prev_w = watts;
        prev_vlim = vlim;

        RELEASE_REPAINT_MUTEX();
        return need_repaint;
    }
    /// @brief Queue an actual hardware repaint (call after all desired changes have been submited to cache via other functions)
    void repaint()
    {
        assert(repaint_task_handle);
        xTaskNotifyGive(repaint_task_handle);
    }
    /// @brief Print a localized message on the screen
    /// @param m See localized_messages
    void print_message(localized_messages m)
    {
        static_assert(ARRAY_SIZE(txt_messages) == localized_messages::TOTAL_MESSAGES);
        assert(m < ARRAY_SIZE(txt_messages));

        ACQUIRE_REPAINT_MUTEX();
        print_str(txt_messages[m]);
        have_to_clear = true;
        if (xResult == pdTRUE) xSemaphoreGive(repaint_mutex);
        else ESP_LOGW(TAG, "Failed to acquire LCD repaint mutex!");
    }
    /// @brief  Print a localized message on the screen with formatting (used for heatup temperature indication during boot, for example)
    /// @param m See localized_messages
    void print_message_f(localized_messages m, ...)
    {
        static_assert(ARRAY_SIZE(txt_messages) == localized_messages::TOTAL_MESSAGES);
        assert(m < ARRAY_SIZE(txt_messages));

        char buffer[MY_DISPLAY_WIDTH * MY_DISPLAY_HEIGHT / (MY_DISPLAY_HEIGHT == 4u ? 2u : 1u) + 1]; //4-line HD44770 displays skip half the lines when printing a single string
        buffer[sizeof(buffer) - 1u] = '\0';
        va_list args;
        va_start(args, m);
        vsnprintf(buffer, sizeof(buffer), txt_messages[m], args);
        va_end(args);

        ACQUIRE_REPAINT_MUTEX();
        print_str(buffer);
        have_to_clear = true;
        if (xResult == pdTRUE) xSemaphoreGive(repaint_mutex);
        else ESP_LOGW(TAG, "Failed to acquire LCD repaint mutex!");
    }
} // namespace menu

/// @brief Repaint task body function. Listens for task notifications and performs complete LCD update according to the display cache.
/// @param pvParameter Not used
static void repaint_task_body(void *pvParameter)
{
    const uint32_t interval_ms = 200;
    static BaseType_t xResult;

    for (;;)
    {
        xResult = ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(interval_ms));
        if (xResult == pdTRUE)
        {
            const position_t pos_vlim = {0, 1};
            const position_t pos_pwr_lbl_pos = {MY_MENU_COLUMN_OFFSET, 0};
            const position_t pos_vlim_lbl_pos = {MY_MENU_COLUMN_OFFSET, 1};

            xResult = xSemaphoreTake(menu::repaint_mutex, pdMS_TO_TICKS(interval_ms));

            if (xResult == pdTRUE)
            {
                if (menu::have_to_clear) my_lcd::clear(lcd_cfg); //1.5mS - long operation that doesn't touch buffers, do not block
                else my_lcd::gotoxy(lcd_cfg, 0, 0);
                my_lcd::puts(lcd_cfg, menu::watts_buffer);
                if (menu::have_to_clear) {
                    my_lcd::gotoxy(lcd_cfg, pos_pwr_lbl_pos.x, pos_pwr_lbl_pos.y);
                    my_lcd::puts(lcd_cfg, txt_units);
                }
                my_lcd::gotoxy(lcd_cfg, pos_vlim.x, pos_vlim.y);
                my_lcd::puts(lcd_cfg, menu::vlim_buffer);
                if (menu::have_to_clear) {
                    my_lcd::gotoxy(lcd_cfg, pos_vlim_lbl_pos.x, pos_vlim_lbl_pos.y);
                    my_lcd::puts(lcd_cfg, txt_units_vlim);
                }
                menu::have_to_clear = false;

                xSemaphoreGive(menu::repaint_mutex);
            }
        }
        else
        {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
}