/**
 * @file main.cpp
 * @author Paul Kutukov
 * @brief Constant power sensor heater control board FW
 * @version 0.1
 * @date 2025-10-29
 *  
 */
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "sdkconfig.h"

#include "menu.h"
#include "params.h"
#include "my_dac.h"
#include "dbg_console.h"
#include "my_hal.h"
#include "modbus.h"

static const char *TAG = "main";

_BEGIN_STD_C
void app_main(void)
{
    static esp_err_t ret;
    static bool init_ok = true;
    static QueueHandle_t dbg_queue; //Interop commands from debug console (for example, calibrations)

    vTaskDelay(pdMS_TO_TICKS(1000));

    //Init NVS
    ret = my_params::init();
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Init failed: params, err: %s", esp_err_to_name(ret));
        init_ok = false;
    }
    //Init HAL
    if (my_hal::init() != ESP_OK)
    {
        ESP_LOGE(TAG, "Init failed: hal");
        init_ok = false;
    }
    //Init DAC calibrations
    my_dac::init(my_params::get_dac_cal());
    //Modbus Slave
    modbus::init(my_hal::get_netif());
    //Display
    ret = menu::init(my_hal::get_lcd_config());
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Init failed: menu. %s", esp_err_to_name(ret));
        init_ok = false;
    }
    else menu::print_message(menu::localized_messages::initializing);
    //Debug console
    dbg_queue = xQueueCreate(4, sizeof(dbg_console::interop_cmd_t));
    dbg_console::init(dbg_queue);

    //Initialization complete
    if (!init_ok)
    {
        ESP_LOGE(TAG, "Init failed. Operation prohibited.");
    }
    else
    {
        menu::repaint();
        my_dac::set_vpwr(0);
        my_dac::set_vlim(5.0f);
        my_hal::set_output_enable(true);
    }

    //Main loop
    static dbg_console::interop_cmd_t dbg_cmd;
    while (1)
    {
        menu::set_watts(my_dac::get_vpwr());

        if (xQueueReceive(dbg_queue, &dbg_cmd, 0) == pdTRUE)
        {
            ESP_LOGI(TAG, "Processing debug interop command #%u...", dbg_cmd.cmd);
            switch (dbg_cmd.cmd) // Blocks
            {
            case dbg_console::interop_cmds::override_errors:
                init_ok = true;
                my_hal::set_output_enable(true);
                break;
            default:
                ESP_LOGW(TAG, "Unknown debug interop command: %i", dbg_cmd.cmd);
                break;
            }
        }

        vTaskDelay(pdMS_TO_TICKS(100));
    }    
}
_END_STD_C