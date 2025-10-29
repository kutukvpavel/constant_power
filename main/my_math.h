#pragma once

#include <inttypes.h>

namespace my_math
{
    float power_to_vpwr(float w);
    float vlim_to_dac_vlim(float v);
    float encoder_to_power(int64_t cnt);
} // namespace my_math
