#include "dbg_console.h"

#include "macros.h"
#include "params.h"
#include "my_hal.h"

#include "linenoise/linenoise.h"
#include "argtable3/argtable3.h"
#include "driver/uart.h"
#include "esp_chip_info.h"
#include "esp_log.h"
#include "esp_flash.h"
#include "driver/uart_vfs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include <stdio.h>
#include <cstring>
#include <string.h>
#include <sys/fcntl.h>

#define PROMPT_STR CONFIG_IDF_TARGET

using namespace my_dbg_helpers;

static const char* TAG = "DBG_MENU";
static const char interactive_prompt[] = LOG_COLOR_I PROMPT_STR "> " LOG_RESET_COLOR;
static const char dumb_prompt[] = PROMPT_STR "> ";
static const char* prompt;
TaskHandle_t parser_task_handle;
QueueHandle_t interop_queue_handle;
static dbg_console::interop_cmd_t interop_cmd;

static void initialize_console();

namespace my_dbg_commands {
    int dump_nvs(int argc, char** argv)
    {
        
        return 0;
    }

    /* 'version' command */
    static int get_version(int argc, char** argv)
    {
        const char* model;
        esp_chip_info_t info;
        uint32_t flash_size;
        esp_chip_info(&info);

        switch (info.model) {
        case CHIP_ESP32:
            model = "ESP32";
            break;
        case CHIP_ESP32S2:
            model = "ESP32-S2";
            break;
        case CHIP_ESP32S3:
            model = "ESP32-S3";
            break;
        case CHIP_ESP32C3:
            model = "ESP32-C3";
            break;
        case CHIP_ESP32H2:
            model = "ESP32-H2";
            break;
        default:
            model = "Unknown";
            break;
        }
        if (esp_flash_get_size(NULL, &flash_size) != ESP_OK) {
            printf("Get flash size failed");
            return 1;
        }
        printf("IDF Version:%s\r\n", esp_get_idf_version());
        printf("Chip info:\r\n");
        printf("\tmodel:%s\r\n", model);
        printf("\tcores:%d\r\n", info.cores);
        printf("\tfeature:%s%s%s%s%" PRIu32 "%s\r\n",
            info.features & CHIP_FEATURE_WIFI_BGN ? "/802.11bgn" : "",
            info.features & CHIP_FEATURE_BLE ? "/BLE" : "",
            info.features & CHIP_FEATURE_BT ? "/BT" : "",
            info.features & CHIP_FEATURE_EMB_FLASH ? "/Embedded-Flash:" : "/External-Flash:",
            flash_size / (1024 * 1024), " MB");
        printf("\trevision number:%d\r\n", info.revision);
        printf("FW ver = %s\r\n", FIRMWARE_VERSION_STR);
        return 0;
    }
    static int reboot(int argc, char** argv)
    {
        esp_restart();
    }
    static int reset_nvs(int argc, char** argv)
    {
        return my_params::factory_reset();
    }
    static int save_nvs(int argc, char** argv)
    {
        return my_params::save();
    }
    static int set_vpwr_cal(int argc, char** argv)
    {
        static my_dac_cal_t c = { 0 };

        if (argc < 2) return 1;
        c.offset_vpwr = my_params::get_dac_cal()->offset_vpwr;
        int ret = sscanf(argv[1], "%f", &(c.gain_vpwr));
        if (!ret) return 2;
        if (argc > 2)
        {
            ret = sscanf(argv[2], "%f", &(c.offset_vpwr));
            if (!ret) return 2;
        }
        my_params::set_dac_cal(&c);
        return 0;
    }
    static int set_vlim_cal(int argc, char** argv)
    {
        static my_dac_cal_t c = { 0 };

        if (argc < 2) return 1;
        c.offset_vlim = my_params::get_dac_cal()->offset_vlim;
        int ret = sscanf(argv[1], "%f", &(c.gain_vlim));
        if (!ret) return 2;
        if (argc > 2)
        {
            ret = sscanf(argv[2], "%f", &(c.offset_vlim));
            if (!ret) return 2;
        }
        my_params::set_dac_cal(&c);
        return 0;
    }
    static int set_sn(int argc, char** argv)
    {
        if (argc < 2) return 1;
        if (strnlen(argv[1], INFO_STR_MAX_LEN + 1) > INFO_STR_MAX_LEN) return 2;
        my_params::set_serial_number(argv[1]);
        return 0;
    }
    static int set_pcb(int argc, char** argv)
    {
        if (argc < 2) return 1;
        if (strnlen(argv[1], INFO_STR_MAX_LEN + 1) > INFO_STR_MAX_LEN) return 2;
        my_params::set_pcb_revision(argv[1]);
        return 0;
    }
    static int test_nvs_crc(int argc, char** argv)
    {
        my_params::test_crc_dbg();
        return 0;
    }
    static int reset_dev_info(int argc, char** argv)
    {
        my_params::reset_dev_info_dbg();
        return 0;
    }
    static int set_pwr(int argc, char** argv)
    {
        if (argc < 2) return 1;
        float val;
        int read = sscanf(argv[1], "%f", &val);
        if (read < 1) return 2;
        my_dac::set_vpwr(val);
        return 0;
    }
    static int set_vlim(int argc, char** argv)
    {
        if (argc < 2) return 1;
        float val;
        int read = sscanf(argv[1], "%f", &val);
        if (read < 1) return 2;
        my_dac::set_vlim(val);
        return 0;
    }
    static int override_error(int argc, char** argv)
    {
        my_dbg_helpers::interop_enqueue(dbg_console::interop_cmds::override_errors, NULL);
        return 0;
    }
    static int log_set_debug(int argc, char** argv)
    {
        esp_log_level_set("*", esp_log_level_t::ESP_LOG_DEBUG);
        return 0;
    }
    static int get_reset_reason(int argc, char** argv)
    {
        return esp_reset_reason();
    }
    static int get_free_heap(int argc, char** argv)
    {
        printf("%u\n", xPortGetFreeHeapSize());
        return 0;
    }
    static int set_dac_soft_sentinel(int argc, char** argv)
    {
        float sentinel;
        if (argc < 2) return 1;
        if (sscanf(argv[1], "%f", &sentinel) != 1) return 2;
        if (sentinel < 0 || sentinel > 4) return 3;
        my_params::set_dac_soft_sentinel(sentinel);
        return 0;
    }
}

static const esp_console_cmd_t commands[] = {
    { .command = "dump_nvs",
        .help = "Dump NVS data",
        .hint = NULL,
        .func = &my_dbg_commands::dump_nvs },
    { .command = "version",
        .help = "Get version of chip and SDK",
        .hint = NULL,
        .func = &my_dbg_commands::get_version },
    { .command = "reboot",
        .help = "Software reset",
        .hint = NULL,
        .func = &my_dbg_commands::reboot },
    { .command = "reset_nvs",
        .help = "Erase NVS storage section (reset required to load defaults)",
        .hint = NULL,
        .func = &my_dbg_commands::reset_nvs },
    { .command = "save_nvs",
        .help = "Save configuration to NVS",
        .hint = NULL,
        .func = &my_dbg_commands::save_nvs },
    { .command = "set_dac_cal",
        .help = "Set DAC calibration (gain [offset]). Save NVS for this setting to persist.",
        .hint = NULL,
        .func = &my_dbg_commands::set_vpwr_cal },
    { .command = "set_dac_cal",
        .help = "Set DAC calibration (gain [offset]). Save NVS for this setting to persist.",
        .hint = NULL,
        .func = &my_dbg_commands::set_vlim_cal },
    { .command = "set_sn",
        .help = "Set device S/N (string up to 31 characters long)",
        .hint = NULL,
        .func = &my_dbg_commands::set_sn },
    { .command = "set_pcb",
        .help = "Set pcb rev (string up to 31 characters long)",
        .hint = NULL,
        .func = &my_dbg_commands::set_pcb },
    { .command = "test_nvs_crc",
        .help = "Set CRC to 0",
        .hint = NULL,
        .func = &my_dbg_commands::test_nvs_crc },
    { .command = "reset_dev_info",
        .help = "Reset device info SPIFFS file",
        .hint = NULL,
        .func = &my_dbg_commands::reset_dev_info },
    { .command = "set_pwr",
        .help = "Set output power",
        .hint = NULL,
        .func = &my_dbg_commands::set_pwr },
    { .command = "set_vlim",
        .help = "Set overvoltage protection threshold",
        .hint = NULL,
        .func = &my_dbg_commands::set_vlim },
    { .command = "override_error",
        .help = "Override any startup error",
        .hint = NULL,
        .func = &my_dbg_commands::override_error },
    { .command = "log_set_debug",
        .help = "Set log level to DEBUG. This action can be undone only by a reset.",
        .hint = NULL,
        .func = &my_dbg_commands::log_set_debug },
    { .command = "get_reset_reason",
        .help = "Returns reset reason code",
        .hint = NULL,
        .func = &my_dbg_commands::get_reset_reason },
    { .command = "get_free_heap",
        .help = "Prints free heap memory according to FreeRTOS",
        .hint = NULL,
        .func = &my_dbg_commands::get_free_heap },
    { .command = "set_dac_soft_sentinel",
        .help = "Set DAC soft sentinel threshold (hard sentinel = 3.8V)",
        .hint = NULL,
        .func = &my_dbg_commands::set_dac_soft_sentinel }
};

/// @brief Figure out if the terminal supports escape sequences
static void probe_terminal()
{
    ESP_LOGI(TAG, "Will now probe...");
    int probe_status = linenoiseProbe();
    if (probe_status) { /* zero indicates success */
        printf("\n"
               "Your terminal application does not support escape sequences.\n"
               "Line editing and history features are disabled.\n"
               "On Windows, try using Putty instead. Status: %d\n", probe_status);
        linenoiseSetDumbMode(1);
#if CONFIG_LOG_COLORS
        /* Since the terminal doesn't support escape sequences,
         * don't use color codes in the prompt.
         */
        prompt = dumb_prompt;
#endif // CONFIG_LOG_COLORS
    }
    else
    {
        printf("\n"
           "Type 'help' to get the list of commands.\n"
           "Use UP/DOWN arrows to navigate through command history.\n"
           "Press TAB when typing command name to auto-complete.\n");
        linenoiseSetDumbMode(0);
#if CONFIG_LOG_COLORS
        prompt = interactive_prompt;
#endif // CONFIG_LOG_COLORS
    }
}
/// @brief Initialize esp console, lineNoise library and install uart VFS drivers, redirecting stdout into the console.
static void initialize_console()
{
    setvbuf(stdin, NULL, _IONBF, 0);
    ESP_ERROR_CHECK(uart_driver_install((uart_port_t)CONFIG_ESP_CONSOLE_UART_NUM,
        256, 0, 0, NULL, 0));
    uart_vfs_dev_port_set_rx_line_endings(CONFIG_ESP_CONSOLE_UART_NUM, ESP_LINE_ENDINGS_CR);
    uart_vfs_dev_use_driver(CONFIG_ESP_CONSOLE_UART_NUM);
    /* Enable blocking mode on stdin, otherwise current linenoise won't work */
    ESP_ERROR_CHECK_WITHOUT_ABORT(fcntl(fileno(stdin), F_SETFL, 0));

    /* Initialize the console */
    esp_console_config_t console_config = {
        .max_cmdline_length = 256,
        .max_cmdline_args = 8,
#if CONFIG_LOG_COLORS
        .hint_color = atoi(LOG_COLOR_CYAN)
#endif
    };
    ESP_ERROR_CHECK(esp_console_init(&console_config));

    /* Configure linenoise line completion library */
    /* Enable multiline editing. If not set, long commands will scroll within
     * single line.
     */
    linenoiseSetMultiLine(1);

    /* Tell linenoise where to get command completions and hints */
    linenoiseSetCompletionCallback((linenoiseCompletionCallback*)&esp_console_get_completion);
    linenoiseSetHintsCallback((linenoiseHintsCallback*)&esp_console_get_hint);

    /* Set command history size */
    linenoiseHistorySetMaxLen(32);

    /* Set command maximum length */
    linenoiseSetMaxLineLen(console_config.max_cmdline_length);

    /* Don't return empty lines */
    linenoiseAllowEmpty(false);

#if CONFIG_STORE_HISTORY
    /* Load command history from filesystem */
    linenoiseHistoryLoad(HISTORY_PATH);
#endif

    /* Register commands */
    esp_console_register_help_command();
    my_dbg_helpers::register_cmds(commands, ARRAY_SIZE(commands));

    /* Prompt to be printed before each line.
     * This can be customized, made dynamic, etc.
     */
    prompt =
#if CONFIG_LOG_COLORS
    interactive_prompt
#else
    dumb_prompt
#endif
    ;

    probe_terminal();
    //linenoiseSetDumbMode(0);
}
/// @brief Console input parser task body function.
/// @param arg Not used
static void parser_task(void* arg)
{
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(20));
        /* Get a line using linenoise.
         * The line is returned when ENTER is pressed.
         */
        char* line = linenoise(prompt);
        if (line == NULL) { /* Break on EOF or error */
            continue;
        }
        /* Add the command to the history if not empty*/
        if (strlen(line) > 0) {
            linenoiseHistoryAdd(line);
#if CONFIG_STORE_HISTORY
            /* Save command history to filesystem */
            linenoiseHistorySave(HISTORY_PATH);
#endif
        }
        else continue;

        /* Try to run the command */
        int ret;
        esp_err_t err = esp_console_run(line, &ret);
        if (err == ESP_ERR_NOT_FOUND) {
            ESP_LOGW(TAG, "Unrecognized command: '%s'\n", line);
        } else if (err == ESP_ERR_INVALID_ARG) {
            // command was empty
        } else if (err == ESP_OK && ret != ESP_OK) {
            ESP_LOGW(TAG, "Command returned non-zero error code: 0x%x (%s)\n", ret, esp_err_to_name(ret));
        } else if (err != ESP_OK) {
            ESP_LOGE(TAG, "Internal error: %s\n", esp_err_to_name(err));
        }
        /* linenoise allocates line buffer on the heap, so need to free it */
        linenoiseFree(line);
    }
}

namespace my_dbg_helpers
{
    /// @brief Boolean argument parser helper function
    /// @param argc from console
    /// @param argv from console
    /// @return True if argument was '1' or if there was no argument. False otherwise.
    bool bool_arg_helper(int argc, char** argv)
    {
        if (argc > 1) {
            char* a = argv[1];
            return *a == '1';
        } else {
            return true;
        }
    }
    /// @brief A helper function to register an array of console commands
    /// @param arr array
    /// @param len array length
    void register_cmds(const esp_console_cmd_t* arr, size_t len)
    {
        for (size_t i = 0; i < len; i++)
        {
            esp_console_cmd_register(&(arr[i]));
        }
    }
    /// @brief Try to enqueue a new interoperability command.
    /// @param cmd Interop command
    /// @param arg Interop arguments
    /// @return True if succeeded, False otherwise.
    bool interop_enqueue(dbg_console::interop_cmds cmd, void* arg)
    {
        assert(interop_queue_handle);

        interop_cmd.cmd = cmd;
        interop_cmd.args = arg;
        if (xQueueSend(interop_queue_handle, &interop_cmd, 0) != pdTRUE)
        {
            printf("Failed to enqueue a new debug interoperation. Please wait for previous ones to finish.\n");
            return false;
        }
        else
        {
            ESP_LOGD(TAG, "Enqued interop message");
        }
        return true;
    }
}


namespace dbg_console {
    /// @brief Initialize common debug console and lineNoise, install uart VFS driver. Creates debug console task.
    /// @param interop_queue Interoperablity queue used to execute debug commands like calibrations and error overrides
    void init(QueueHandle_t interop_queue)
    {
        ESP_LOGI(TAG, "Initializing...");
        assert(interop_queue);

        interop_queue_handle = interop_queue;
        initialize_console();
        xTaskCreate(parser_task, "dbg_console_parser", 10000, NULL, 1, &parser_task_handle);
    }
}