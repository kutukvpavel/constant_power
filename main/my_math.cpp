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
        static_assert(MY_VLIM_MAX < 8.8f);
        static_assert(MY_VLIM_MIN >= 1.25f);
        return 5.831f - 0.66f * v;
    }
    float encoder_to_power(int64_t cnt)
    {
        return static_cast<float>(cnt) * ENCODER_RESOLUTION_STEP;
    }
} // namespace my_math
