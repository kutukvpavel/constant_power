#pragma once

#include <inttypes.h>

struct my_dac_cal_t
{
    float gain_vpwr;
    float offset_vpwr;
    float gain_vlim;
    float offset_vlim;
};

namespace my_dac
{
    void init(const my_dac_cal_t* cal);
    void set_vpwr(float volt);
    float get_vpwr();
    void set_vlim(float volt);
    float get_vlim();

    void soft_heat_up(float target_volts, float time_seconds);
    void soft_cool_down(float time_seconds);
}