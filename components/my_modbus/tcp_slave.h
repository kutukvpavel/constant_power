#pragma once

#include <esp_err.h>
#include "mbcontroller.h"       // for mbcontroller defines and api
#include "modbus_params.h"      // for modbus parameters structures

esp_err_t slave_init(mb_communication_info_t* comm_info);
