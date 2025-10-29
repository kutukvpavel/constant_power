#include "modbus.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <esp_log.h>

#include "tcp_slave.h"
#include "mbcontroller.h"

#include "my_hal.h"

namespace modbus
{
    static const char *TAG = "MY_MODBUS";
    static TaskHandle_t mb_slave_loop_handle = NULL;
    static void* slave_handle = NULL;

    void mb_event_cb(const mb_param_info_t* reg_info)
    {
        const char* rw_str = (reg_info->type & MB_READ_MASK) ? "READ" : "WRITE";
        int sw_type = reg_info->type & MB_READ_WRITE_MASK;
        // Filter events and process them accordingly
        switch (sw_type)
        {
        case (MB_EVENT_HOLDING_REG_WR | MB_EVENT_HOLDING_REG_RD):
            // Get parameter information from parameter queue
            ESP_LOGI(TAG, "HOLDING %s (%" PRIu32 " us), ADDR:%u, TYPE:%u, INST_ADDR:0x%" PRIx32 ", SIZE:%u",
                    rw_str,
                    reg_info->time_stamp,
                    (unsigned)reg_info->mb_offset,
                    (unsigned)reg_info->type,
                    (uint32_t)reg_info->address,
                    (unsigned)reg_info->size);
            
            break;
        case MB_EVENT_INPUT_REG_RD:
            ESP_LOGI(TAG, "INPUT READ (%" PRIu32 " us), ADDR:%u, TYPE:%u, INST_ADDR:0x%" PRIx32 ", SIZE:%u",
                    reg_info->time_stamp,
                    (unsigned)reg_info->mb_offset,
                    (unsigned)reg_info->type,
                    (uint32_t)reg_info->address,
                    (unsigned)reg_info->size);
            break;
        case MB_EVENT_DISCRETE_RD:
            ESP_LOGI(TAG, "DISCRETE READ (%" PRIu32 " us), ADDR:%u, TYPE:%u, INST_ADDR:0x%" PRIx32 ", SIZE:%u",
                    reg_info->time_stamp,
                    (unsigned)reg_info->mb_offset,
                    (unsigned)reg_info->type,
                    (uint32_t)reg_info->address,
                    (unsigned)reg_info->size);
            break;
        case MB_EVENT_COILS_RD | MB_EVENT_COILS_WR:
            ESP_LOGI(TAG, "COILS %s (%" PRIu32 " us), ADDR:%u, TYPE:%u, INST_ADDR:0x%" PRIx32 ", SIZE:%u",
                    rw_str,
                    reg_info->time_stamp,
                    (unsigned)reg_info->mb_offset,
                    (unsigned)reg_info->type,
                    (uint32_t)reg_info->address,
                    (unsigned)reg_info->size);
            break;
        default:
            break;
        }
    }

    void init(esp_netif_t* netif_ptr)
    {
        //Start modbus slave server
        ESP_ERROR_CHECK(init_services());
        mb_communication_info_t tcp_slave_config = {
            .tcp_opts = {
                .mode = MB_TCP,
                .port = MB_TCP_PORT_NUMBER,
                .uid = MB_SLAVE_ADDR,
#if !CONFIG_EXAMPLE_CONNECT_IPV6
                .addr_type = mb_addr_type_t::MB_IPV4,
#else
                .addr_type = MB_IPV6,
#endif
                .ip_addr_table = NULL, // Bind to any address
                .ip_netif_ptr = netif_ptr
            }
        };
        ESP_ERROR_CHECK(slave_init(&tcp_slave_config, mb_event_cb, &slave_handle));
        assert(slave_handle);
        // The Modbus slave logic is located in this function (user handling of Modbus)
        xTaskCreate(slave_operation_func, "mb_slave_loop", 4096, NULL, 1, &mb_slave_loop_handle);
        assert(mb_slave_loop_handle);
    }

    bool get_remote_enabled()
    {
        if (!slave_handle) return false;
        mbc_slave_lock(slave_handle);
        bool enabled = coil_reg_params.coil_0 > 0;
        mbc_slave_unlock(slave_handle);
        return enabled;
    }
    float get_pwr_setpoint()
    {
        assert(slave_handle);
        mbc_slave_lock(slave_handle);
        if (holding_reg_params.power_setpoint < 0)
        {
            holding_reg_params.power_setpoint = 0;
        }
        else if (holding_reg_params.power_setpoint > MY_PWR_MAX)
        {
            holding_reg_params.power_setpoint = MY_PWR_MAX;
        }
        float ret = holding_reg_params.power_setpoint;
        mbc_slave_unlock(slave_handle);
        return ret;
    }
    float get_vlim_setpoint()
    {
        assert(slave_handle);
        mbc_slave_lock(slave_handle);
        if (holding_reg_params.vlim_setpoint < MY_VLIM_MIN)
        {
            holding_reg_params.vlim_setpoint = MY_VLIM_MIN;
        }
        else if (holding_reg_params.vlim_setpoint > MY_VLIM_MAX)
        {
            holding_reg_params.vlim_setpoint = MY_VLIM_MAX;
        }
        float ret = holding_reg_params.vlim_setpoint;
        mbc_slave_unlock(slave_handle);
        return ret;
    }

    void set_values(bool is_on, float pwr, float vlim, float vpwr, float dac_vlim)
    {
        if (!slave_handle) return;
        mbc_slave_lock(slave_handle);
        discrete_reg_params.discrete_input0 = (is_on ? 1 : 0);
        input_reg_params.power_man = pwr;
        input_reg_params.vlim_man = vlim;
        input_reg_params.vpwr = vpwr;
        input_reg_params.dac_vlim = dac_vlim;
        mbc_slave_unlock(slave_handle);
    }
    void disable_remote()
    {
        assert(slave_handle);
        mbc_slave_lock(slave_handle);
        coil_reg_params.coil_0 = 0;
        mbc_slave_unlock(slave_handle);
    }
} // namespace modbus
