#include "my_math.h"

#include "my_hal.h"

namespace my_math
{
    float power_to_vpwr(float w)
    {
        return w;
    }
    float vlim_to_dac_vlim(float v)
    {
        static_assert(MY_VLIM_MAX < 17.6f);
        return 5.81335f - 0.33f * v; //17.6 V max
    }
    float encoder_to_power(int64_t cnt)
    {
        return static_cast<float>(cnt) * ENCODER_RESOLUTION_STEP;
    }
} // namespace my_math
