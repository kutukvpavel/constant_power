/**
 * @file my_dac.cpp
 * @author MSU
 * @brief This unit provides DAC abstraction, taking care about N(V) mapping (N = DAC code, V = target heater amplifier voltage),
 * output range limits as well as soft ramp up/down.
 * @date 2024-11-28
 * 
 */

#include "my_dac.h"

#include "macros.h"
#include "my_hal.h"
#include "params.h"

#include <esp_log.h>
#include <math.h>

#define MY_DAC_VPWR_FULL_SCALE 0x03FF //10-bit DAC
#define MY_DAC_VLIM_FULL_SCALE 0x00FF //8-bit DAC
#define MY_DAC_ZERO_SCALE 0x0000
#define MY_DAC_VPWR_SENTINEL 0x03F0
#define MY_DAC_SR_VLIM_OFFSET (2u * 8u)

static const char* TAG = "DAC";

const my_dac_cal_t* calibration = &my_params::default_dac_cal;
/// @brief Last target voltage set
float last_vpwr = 0;
float last_vlim = 0;
my_hal::dac_code_t last_code = 0;

namespace my_dac {
    /// @brief Initialize DAC-abstraction (sets the N(V) "calibration" data)
    /// @param cal DAC cal coefficients from my_params
    void init(const my_dac_cal_t* cal)
    {
        calibration = cal;
    }
    /// @brief Set sensor heater amplifier output voltage directly.
    /// @param volt Target voltage, volts.
    void set_vpwr(float volt)
    {
        if (!isfinite(volt)) {
            ESP_LOGW(TAG, "DAC ignored infinte value: %f", volt);
            return;
        }
        last_vpwr = volt;
        if (volt > my_params::get_dac_soft_sentinel())
        {
            volt = my_params::get_dac_soft_sentinel();
            ESP_LOGD(TAG, "Soft sentinel reached");
        }
        volt = volt * calibration->gain_vpwr + 0.5 + calibration->offset_vpwr;
        if (volt > MY_DAC_VPWR_FULL_SCALE)
            volt = MY_DAC_VPWR_FULL_SCALE;
        else if (volt < MY_DAC_ZERO_SCALE)
            volt = MY_DAC_ZERO_SCALE;
        if (volt > MY_DAC_VPWR_SENTINEL) 
        {
            volt = MY_DAC_VPWR_SENTINEL;
            ESP_LOGD(TAG, "Sentinel reached.");
        }
        last_code &= ~(MY_DAC_VPWR_FULL_SCALE); 
        my_hal::dac_code_t code = static_cast<my_hal::dac_code_t>(volt);
        last_code |= (code >> 2u) & 0xFF;
        last_code |= (code & 0b11) << 8u;
        my_hal::sr_write(my_hal::sr_types::SR_DAC, reinterpret_cast<uint8_t*>(&last_code));
    }
    /// @brief Get last set heater amplifier output voltage
    /// @return Volts
    float get_vpwr()
    {
        return last_vpwr;
    }
    /// @brief 
    /// @param volt Target voltage, volts.
    void set_vlim(float volt)
    {
        if (!isfinite(volt)) {
            ESP_LOGW(TAG, "DAC ignored infinte value: %f", volt);
            return;
        }
        last_vlim = volt;
        volt = volt * calibration->gain_vlim + 0.5 + calibration->offset_vlim;
        if (volt > MY_DAC_VLIM_FULL_SCALE)
            volt = MY_DAC_VLIM_FULL_SCALE;
        else if (volt < MY_DAC_ZERO_SCALE)
            volt = MY_DAC_ZERO_SCALE;
        last_code &= ~(MY_DAC_VLIM_FULL_SCALE << MY_DAC_SR_VLIM_OFFSET);
        last_code |= static_cast<my_hal::dac_code_t>(volt) << MY_DAC_SR_VLIM_OFFSET;
        my_hal::sr_write(my_hal::sr_types::SR_DAC, reinterpret_cast<uint8_t*>(&last_code));
    }
    /// @brief 
    /// @return Volts
    float get_vlim()
    {
        return last_vlim;
    }
    /// @brief Execute linear heating profile (from 0 volts to target_volts in time_seconds)
    /// @param target_volts Volts
    /// @param time_seconds Seconds
    void soft_heat_up(float target_volts, float time_seconds)
    {
        static const uint32_t time_step_ms = 5;
        assert(isfinite(target_volts));
        assert(isfinite(time_seconds));
        assert(time_seconds > 0);

        uint32_t cycles = static_cast<uint32_t>(time_seconds * 1000 / time_step_ms + 1.5f);
        float voltage_step = target_volts / cycles;
        TickType_t previous_wake = xTaskGetTickCount();
        ESP_LOGI(TAG, "Soft heatup params: cycles = %" PRIu32 ", step = %.3f", cycles, voltage_step);

        for (uint32_t i = 1; i <= cycles; i++)
        {
            float v = voltage_step * i;
            set_vpwr(v);
            if (i % (cycles / 10) == 0) printf("Heatup: %.3f\n", v);
            xTaskDelayUntil(&previous_wake, pdMS_TO_TICKS(time_step_ms));
        }
    }
    /// @brief Perform linear cooldown profile (from current voltage to 0 in time_seconds)
    /// @param time_seconds Seconds
    void soft_cool_down(float time_seconds)
    {
        static const uint32_t time_step_ms = 10;
        assert(isfinite(time_seconds));
        assert(time_seconds > 0);

        int32_t cycles = static_cast<int32_t>(time_seconds * 1000 / time_step_ms + 1.5f);
        float voltage_step = get_vpwr() / cycles;
        TickType_t previous_wake = xTaskGetTickCount();
        ESP_LOGI(TAG, "Soft cooldown params: cycles = %" PRIi32 ", step = %.3f", cycles, voltage_step);

        for (int32_t i = cycles - 1; i >= 0; i--)
        {
            float v = voltage_step * i;
            set_vpwr(v);
            if (i % (cycles / 10) == 0) printf("Cooldown: %.3f\n", v);
            xTaskDelayUntil(&previous_wake, pdMS_TO_TICKS(time_step_ms));
        }
    }
}
