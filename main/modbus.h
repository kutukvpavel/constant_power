#pragma once

#include <esp_err.h>
#include <esp_netif.h>

namespace modbus
{
    void init(esp_netif_t* netif_ptr);

    bool get_remote_enabled();
    float get_pwr_setpoint();
    float get_vlim_setpoint();

    void set_values(bool is_on, float pwr, float vlim);
    void disable_remote();
} // namespace modbus
