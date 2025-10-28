#pragma once

#include <esp_err.h>
#include <esp_netif.h>

namespace modbus
{
    void init(esp_netif_t* netif_ptr);
} // namespace modbus
