#include "modbus.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <esp_log.h>

#include "tcp_slave.h"

namespace modbus
{
    static const char *TAG = "MY_MODBUS";
    static TaskHandle_t mb_slave_loop_handle = NULL;

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
        mb_communication_info_t comm_info = {};
#if !CONFIG_EXAMPLE_CONNECT_IPV6
        comm_info.ip_addr_type = MB_IPV4;
#else
        comm_info.ip_addr_type = MB_IPV6;
#endif
        comm_info.ip_mode = MB_MODE_TCP;
        comm_info.ip_port = MB_TCP_PORT_NUMBER;
        comm_info.ip_addr = NULL; // Bind to any address
        comm_info.ip_netif_ptr = netif_ptr;
        comm_info.slave_uid = MB_SLAVE_ADDR;
        ESP_ERROR_CHECK(slave_init(&comm_info, mb_event_cb));
        // The Modbus slave logic is located in this function (user handling of Modbus)
        xTaskCreate(slave_operation_func, "mb_slave_loop", 4096, NULL, 1, &mb_slave_loop_handle);
        assert(mb_slave_loop_handle);
    }
} // namespace modbus
