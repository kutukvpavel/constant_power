/**
 * @file my_hal.cpp
 * @author MSU
 * @brief Hardware Abstraction Layer: GPIO pin layout, hardware timers and PWM (buzzer, LED and analog PSU soft-start),
 * ADC channels, entering and recovering from sleep mode, Shift-Register IO, CPU power management
 * @date 2022-10-21
 * 
 */

#include "my_hal.h"

#include "params.h"
#include "macros.h"
#include "my_dac.h"

#include <esp_log.h>
#include <esp_check.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include <rom/ets_sys.h>
#include <driver/ledc.h>
#include <soc/rtc.h>
#include <soc/rtc_cntl_reg.h>
#include <driver/rtc_io.h>
#include <rom/gpio.h>
#include <esp_eth.h>
#include <esp_event.h>

#include "ethernet_init.h"
#include "ESP32Encoder.h"

#define MAX_CPU_FREQ_MHZ 160
#define DEFAULT_CPU_FREQ_MHZ 80
#define MIN_CPU_FREQ_MHZ 40
#define ENCODER_MAX_COUNTS (MY_PWR_MAX / ENCODER_RESOLUTION_STEP)
#define ENCODER_MIN_COUNTS 0

static const char TAG[] = "HAL";

esp_err_t lcd_write_callback(const my_lcd::hd44780_t* lcd, uint8_t b);

/**
 * Configuration Section: Pin Numbers
 * 
 */

const gpio_num_t pin_btn = GPIO_NUM_35;
const gpio_num_t pin_oe = GPIO_NUM_14;
const gpio_num_t pin_lcd_rs = GPIO_NUM_32;
const gpio_num_t pin_lcd_e = GPIO_NUM_33;
const gpio_num_t pin_enc_a = GPIO_NUM_39;
const gpio_num_t pin_enc_b = GPIO_NUM_36;

const gpio_num_t input_gpio[] =
{
    pin_btn,
    pin_enc_a,
    pin_enc_b
};
const gpio_num_t output_gpio[] =
{
    pin_oe,
    pin_lcd_rs,
    pin_lcd_e
};

/**
 * @brief Shift Registers
 * 
 */
struct my_sr
{
    gpio_num_t d;
    gpio_num_t clk;
    gpio_num_t latch;
    bool msb_first; //Bit order
    size_t len; //Bytes
};

const my_sr regs[] = 
{
    { GPIO_NUM_12, GPIO_NUM_2, GPIO_NUM_4, true, 3 }, // DACs
    { GPIO_NUM_12, GPIO_NUM_15, GPIO_NUM_4, true, 1 } // LCD
};

/// @brief LCD configuration for hd44780 library. Note that the databus is handled externally, because it's driven by a 595 shift register.
static my_lcd::hd44780_t lcd_cfg = 
{
    lcd_write_callback, //!< Data write callback. Set it to NULL in case of direct LCD connection to GPIO
    {
        pin_lcd_rs,        //!< GPIO/register bit used for RS pin
        pin_lcd_e,         //!< GPIO/register bit used for E pin
        GPIO_NUM_NC,
        GPIO_NUM_NC,
        GPIO_NUM_NC,
        GPIO_NUM_NC,
        GPIO_NUM_NC,        //!< GPIO/register bit used for D4 pin
        GPIO_NUM_NC,        //!< GPIO/register bit used for D5 pin
        GPIO_NUM_NC,        //!< GPIO/register bit used for D5 pin
        GPIO_NUM_NC,        //!< GPIO/register bit used for D5 pin
        static_cast<gpio_num_t>(HD44780_NOT_USED)        //!< GPIO/register bit used for backlight. Set it `HD44780_NOT_USED` if no backlight used
    },
    my_lcd::hd44780_font_t::HD44780_FONT_5X8,   //!< LCD Font type
    2,         //!< Number of lines for LCD.
    true        //!< Current backlight state
};

// ENCODER
static ESP32Encoder encoder;

// Ethernet
static uint8_t eth_port_cnt = 0;
static esp_eth_handle_t *eth_handles;
esp_netif_t **eth_netifs;
/** Event handler for Ethernet events */
static void eth_event_handler(void *arg, esp_event_base_t event_base,
                              int32_t event_id, void *event_data)
{
    uint8_t mac_addr[6] = {0};
    /* we can get the ethernet driver handle from event data */
    esp_eth_handle_t eth_handle = *(esp_eth_handle_t *)event_data;

    switch (event_id) {
    case ETHERNET_EVENT_CONNECTED:
        esp_eth_ioctl(eth_handle, ETH_CMD_G_MAC_ADDR, mac_addr);
        ESP_LOGI(TAG, "Ethernet Link Up");
        ESP_LOGI(TAG, "Ethernet HW Addr %02x:%02x:%02x:%02x:%02x:%02x",
                 mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);
        break;
    case ETHERNET_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "Ethernet Link Down");
        break;
    case ETHERNET_EVENT_START:
        ESP_LOGI(TAG, "Ethernet Started");
        break;
    case ETHERNET_EVENT_STOP:
        ESP_LOGI(TAG, "Ethernet Stopped");
        break;
    default:
        break;
    }
}
/** Event handler for IP_EVENT_ETH_GOT_IP */
static void got_ip_event_handler(void *arg, esp_event_base_t event_base,
                                 int32_t event_id, void *event_data)
{
    ip_event_got_ip_t *event = (ip_event_got_ip_t *) event_data;
    const esp_netif_ip_info_t *ip_info = &event->ip_info;

    ESP_LOGI(TAG, "Ethernet Got IP Address");
    ESP_LOGI(TAG, "~~~~~~~~~~~");
    ESP_LOGI(TAG, "ETHIP:" IPSTR, IP2STR(&ip_info->ip));
    ESP_LOGI(TAG, "ETHMASK:" IPSTR, IP2STR(&ip_info->netmask));
    ESP_LOGI(TAG, "ETHGW:" IPSTR, IP2STR(&ip_info->gw));
    ESP_LOGI(TAG, "~~~~~~~~~~~");
}

/**
 * PUBLIC routines
 * 
 */

namespace my_hal
{
    /// @brief Initialize HAL and the peripherals it contorls.
    /// @param buzzer_freq Buzzer frequency (driving square wave frequency, harmonics will be present)
    /// @param shdn_pump_freq Driving frequency for the charge pump that drives the gate of the MOSFET that enables analog PSU
    /// @return ESK_OK, or panics otherwise
    esp_err_t init()
    {
        const uint32_t zero = 0;
        const uint8_t* const zero_ptr = reinterpret_cast<const uint8_t*>(&zero);
        ESP_LOGI(TAG, "HAL initialization");

        ESP_LOGI(TAG, "Init GPIO direction...");
        //Output pins
        for (auto &&i : output_gpio)
        {
            gpio_pad_select_gpio(i);
            ESP_ERROR_CHECK(gpio_set_direction(i, GPIO_MODE_OUTPUT));
            ESP_ERROR_CHECK(gpio_set_level(i, i == pin_oe ? 1 : 0));
        }
        for (auto &&i : input_gpio)
        {
            gpio_pad_select_gpio(i);
            ESP_ERROR_CHECK(gpio_set_direction(i, GPIO_MODE_INPUT));
            ESP_ERROR_CHECK(gpio_set_pull_mode(i, GPIO_FLOATING));
        }

        ESP_LOGI(TAG, "Init SRs...");
        //Set shift register pins as outputs and load all zeros
        for (size_t i = 0; i < ARRAY_SIZE(regs); i++)
        {
            gpio_set_direction(regs[i].d, GPIO_MODE_OUTPUT);
            gpio_set_direction(regs[i].clk, GPIO_MODE_OUTPUT);
            gpio_set_direction(regs[i].latch, GPIO_MODE_OUTPUT);
            assert(regs[i].len <= sizeof(zero));
            sr_write(static_cast<sr_types>(i), zero_ptr);
        }
        set_output_enable(true);

        ESP_LOGI(TAG, "Init encoder...");
        ESP32Encoder::useInternalWeakPullResistors = puType::none;
        encoder.attachHalfQuad(pin_enc_a, pin_enc_b);

        // Initialize Ethernet driver
        ESP_LOGI(TAG, "Init ethernet...");
        ESP_ERROR_CHECK(example_eth_init(&eth_handles, &eth_port_cnt));
        eth_netifs = new esp_netif_t* [eth_port_cnt];
        // Initialize TCP/IP network interface aka the esp-netif (should be called only once in application)
        ESP_ERROR_CHECK(esp_netif_init());
        // Create default event loop that running in background
        ESP_ERROR_CHECK(esp_event_loop_create_default());
        esp_eth_netif_glue_handle_t eth_netif_glues[eth_port_cnt];
        // Create instance(s) of esp-netif for Ethernet(s)
        if (eth_port_cnt == 1)
        {
            // Use ESP_NETIF_DEFAULT_ETH when just one Ethernet interface is used and you don't need to modify
            // default esp-netif configuration parameters.
            esp_netif_config_t cfg = ESP_NETIF_DEFAULT_ETH();
            eth_netifs[0] = esp_netif_new(&cfg);
            eth_netif_glues[0] = esp_eth_new_netif_glue(eth_handles[0]);
            // Attach Ethernet driver to TCP/IP stack
            ESP_ERROR_CHECK(esp_netif_attach(eth_netifs[0], eth_netif_glues[0]));
        }
        else
        {
            // Use ESP_NETIF_INHERENT_DEFAULT_ETH when multiple Ethernet interfaces are used and so you need to modify
            // esp-netif configuration parameters for each interface (name, priority, etc.).
            esp_netif_inherent_config_t esp_netif_config = ESP_NETIF_INHERENT_DEFAULT_ETH();
            esp_netif_config_t cfg_spi = {
                .base = &esp_netif_config,
                .stack = ESP_NETIF_NETSTACK_DEFAULT_ETH};
            char if_key_str[10];
            char if_desc_str[10];
            char num_str[3];
            for (int i = 0; i < eth_port_cnt; i++)
            {
                itoa(i, num_str, 10);
                strcat(strcpy(if_key_str, "ETH_"), num_str);
                strcat(strcpy(if_desc_str, "eth"), num_str);
                esp_netif_config.if_key = if_key_str;
                esp_netif_config.if_desc = if_desc_str;
                esp_netif_config.route_prio -= i * 5;
                eth_netifs[i] = esp_netif_new(&cfg_spi);
                eth_netif_glues[i] = esp_eth_new_netif_glue(eth_handles[i]);
                // Attach Ethernet driver to TCP/IP stack
                ESP_ERROR_CHECK(esp_netif_attach(eth_netifs[i], eth_netif_glues[i]));
            }
        }
        // Register user defined event handlers
        ESP_ERROR_CHECK(esp_event_handler_register(ETH_EVENT, ESP_EVENT_ANY_ID, &eth_event_handler, NULL));
        ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP, &got_ip_event_handler, NULL));
        // Start Ethernet driver state machine
        for (int i = 0; i < eth_port_cnt; i++)
        {
            ESP_ERROR_CHECK(esp_eth_start(eth_handles[i]));
        }

        ESP_LOGI(TAG, "HAL init finished");
        return ESP_OK;
    }

    /// @brief Get HD44780 LCD library configuration
    /// @return Configuration structure
    my_lcd::hd44780_t* get_lcd_config()
    {
        return &lcd_cfg;
    }
    int64_t get_encoder_counts()
    {
        int64_t c = encoder.getCount();
        if (c > ENCODER_MAX_COUNTS)
        {
            c = ENCODER_MAX_COUNTS;
            encoder.setCount(ENCODER_MAX_COUNTS);
        }
        else if (c < ENCODER_MIN_COUNTS)
        {
            c = ENCODER_MIN_COUNTS;
            encoder.setCount(ENCODER_MIN_COUNTS);
        }
        return c;
    }
    esp_netif_t* get_netif()
    {
        return eth_netifs[0];
    }

    /// @brief Write bytes to a shift register chain
    /// @param t Shift register chain
    /// @param contents Buffer to write from
    void sr_write(sr_types t, const uint8_t* contents)
    {
        const size_t byte_len = 8;
        assert(t < ARRAY_SIZE(regs));
        static_assert(sizeof(dac_code_t) >= 3, "Warning: check DAC shift register length!");

        auto sr = regs[t];
        ESP_ERROR_CHECK(gpio_set_level(sr.latch, 0));
        for (size_t i = 0; i < sr.len; i++)
        {
            for (size_t j = 0; j < byte_len; j++)
            {
                uint32_t mask = 1u << (sr.msb_first ? (byte_len - 1 - j) : j);
                ESP_ERROR_CHECK(gpio_set_level(sr.clk, 0));
                ESP_ERROR_CHECK(gpio_set_level(sr.d, (contents[sr.msb_first ? (sr.len - 1 - i) : i] & mask) > 0));
                ets_delay_us(1);
                ESP_ERROR_CHECK(gpio_set_level(sr.clk, 1));
                ets_delay_us(1);
            }
        }
        ESP_ERROR_CHECK(gpio_set_level(sr.latch, 1));
    }
    /// @brief 
    /// @return True == the button is pressed, false otherwise
    bool get_btn_pressed()
    {
        return gpio_get_level(pin_btn) == 0; //Active LOW
    }
    /// @brief Enable DAC outputs. They should be disabled when analog PSU is not active for power not to leak into analog circuits.
    /// @param v True == enable
    void set_output_enable(bool v)
    {
        ESP_ERROR_CHECK(gpio_set_level(pin_oe, !v)); //Active Low
    }
}

/// @brief Hardware interface implementation for HD4470 LCD library. 
/// @param lcd LCD handle
/// @param b Byte to write to the data bus
/// @return ESP_OK (always)
esp_err_t lcd_write_callback(const my_lcd::hd44780_t* lcd, uint8_t b)
{
    my_hal::sr_write(my_hal::sr_types::SR_LCD, &b);

    return ESP_OK;
}