// console.c
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_console.h"
#include "esp_log.h"
#include "linenoise/linenoise.h"
#include "argtable3/argtable3.h"
#include "config.h"
#include "driver/uart.h"
#include "driver/uart_vfs.h"

#define CONSOLE_MAX_CMDLINE_ARGS 8
#define CONSOLE_MAX_CMDLINE_LENGTH 256
#define CONSOLE_PROMPT_MAX_LEN 32

static char prompt[CONSOLE_PROMPT_MAX_LEN];
static const char* TAG = "console";

// ---------------- Command handler macros (SAFE - just define functions) ----------------
#define DEFINE_SET_STR_CMD(cmd_name, validator_fn, saver_fn) \
    static int cmd_name##_cmd(int argc, char **argv) { \
        int nerr = arg_parse(argc, argv, (void**)&cmd_name##_args); \
        if (nerr) { arg_print_errors(stderr, cmd_name##_args.end, argv[0]); return 1; } \
        const char *input = cmd_name##_args.val->sval[0]; \
        if (!validator_fn(input)) { \
            printf("ERR: Invalid value\n"); \
            return 1; \
        } \
        esp_err_t err = saver_fn(input); \
        if (err != ESP_OK) { \
            printf("ERR: NVS write failed (0x%x)\n", err); \
            return 1; \
        } \
        printf("OK\n"); \
        return 0; \
    }

#define DEFINE_SET_INT_CMD(cmd_name, min_val, max_val, saver_fn) \
    static int cmd_name##_cmd(int argc, char **argv) { \
        int nerr = arg_parse(argc, argv, (void**)&cmd_name##_args); \
        if (nerr) { arg_print_errors(stderr, cmd_name##_args.end, argv[0]); return 1; } \
        int val = cmd_name##_args.val->ival[0]; \
        if (val < (min_val) || val > (max_val)) { \
            printf("ERR: Value must be %d–%d\n", (min_val), (max_val)); \
            return 1; \
        } \
        esp_err_t err = saver_fn((uint16_t)val); \
        if (err != ESP_OK) { \
            printf("ERR: NVS write failed (0x%x)\n", err); \
            return 1; \
        } \
        printf("OK\n"); \
        return 0; \
    }

// ---------------- Argtable structs (only declare, don't initialize) ----------------

static struct { struct arg_str *val; struct arg_end *end; } set_ssid_args;
static struct { struct arg_str *val; struct arg_end *end; } set_pass_args;
static struct { struct arg_str *val; struct arg_end *end; } set_ip_args;
static struct { struct arg_str *val; struct arg_end *end; } set_mask_args;
static struct { struct arg_str *val; struct arg_end *end; } set_gw_args;
static struct { struct arg_str *val; struct arg_end *end; } set_sespass_args;
static struct { struct arg_int *val; struct arg_end *end; } set_port_args;

// ---------------- Define command handlers using macros ----------------

DEFINE_SET_STR_CMD(set_ssid,     is_valid_ssid,     config_save_ssid)
DEFINE_SET_STR_CMD(set_pass,     is_valid_password, config_save_password)
DEFINE_SET_STR_CMD(set_ip,       is_valid_ipv4,     config_save_ip)
DEFINE_SET_STR_CMD(set_mask,     is_valid_ipv4,     config_save_netmask)
DEFINE_SET_STR_CMD(set_gw,       is_valid_ipv4,     config_save_gateway)
DEFINE_SET_STR_CMD(set_sespass,  is_valid_sespass,  config_save_sespass)
DEFINE_SET_INT_CMD(set_port,     1, 65535,          config_save_port)

// ---------------- Non-SET commands ----------------

static int conf_cmd(int argc, char **argv) {
    if (config_load() != ESP_OK) {
        printf("Warning: Using current RAM config (NVS reload failed)\n");
    }

    bool valid = config_is_valid();
    printf("=== Configuration Status ===\n");
    printf("%s\n\n", valid ? "Device is FULLY CONFIGURED" : "Device is INCOMPLETELY CONFIGURED");

    printf("=== Configuration Values ===\n");
    printf("ssid:    %s\n", g_config.ssid[0]  ? g_config.ssid  : "(not set)");
    printf("pass:    %s\n", g_config.password[0] ? "*** (set)" : "(not set)");
    printf("ip:      %s\n", g_config.ip[0]      ? g_config.ip   : "(not set)");
    printf("mask:    %s\n", g_config.netmask[0] ? g_config.netmask : "(not set)");
    printf("gw:      %s\n", g_config.gateway[0] ? g_config.gateway : "(not set)");
    printf("port:    %d\n", g_config.port);
    printf("sespass: %s\n", g_config.sespass[0] ? "*** (set)" : "(not set)");
    return 0;
}

static int reboot_cmd(int argc, char **argv) {
    printf("Rebooting...\n");
    fflush(stdout);
    vTaskDelay(pdMS_TO_TICKS(100));
    esp_restart();
    return 0;
}

static int reset_cmd(int argc, char **argv) {
    config_reset_to_factory();
    printf("Factory reset done. Rebooting to apply.\n");
    reboot_cmd(0, NULL);
    return 0;
}

// ---------------- Register commands (initialize argtable HERE) ----------------

void console_register_commands(void) {
    // Initialize argtable structs (MUST be done in function)
    set_ssid_args.val = arg_str1(NULL, NULL, "<ssid>", "WiFi SSID (1-32 chars)");
    set_ssid_args.end = arg_end(1);

    set_pass_args.val = arg_str1(NULL, NULL, "<pass>", "WiFi password (0-64 chars)");
    set_pass_args.end = arg_end(1);

    set_ip_args.val = arg_str1(NULL, NULL, "<ip>", "Static IP address");
    set_ip_args.end = arg_end(1);

    set_mask_args.val = arg_str1(NULL, NULL, "<mask>", "Network mask");
    set_mask_args.end = arg_end(1);

    set_gw_args.val = arg_str1(NULL, NULL, "<gw>", "Gateway IP");
    set_gw_args.end = arg_end(1);

    set_port_args.val = arg_int1(NULL, NULL, "<port>", "Listen port (1-65535)");
    set_port_args.end = arg_end(1);

    set_sespass_args.val = arg_str1(NULL, NULL, "<password>", "Session password (0-64 chars, empty to disable)");
    set_sespass_args.end = arg_end(1);

    // Register commands
    const esp_console_cmd_t cmds[] = {
        { .command = "conf",     .help = "Show current configuration",    .func = conf_cmd },
        { .command = "reset",    .help = "Reset to factory defaults",     .func = reset_cmd },
        { .command = "reboot",   .help = "Reboot the device",             .func = reboot_cmd },
        { .command = "ssid",     .help = "Set WiFi SSID",                 .func = set_ssid_cmd,     .argtable = &set_ssid_args },
        { .command = "pass",     .help = "Set WiFi password",             .func = set_pass_cmd,     .argtable = &set_pass_args },
        { .command = "ip",       .help = "Set static IP",                 .func = set_ip_cmd,       .argtable = &set_ip_args },
        { .command = "mask",     .help = "Set network mask",              .func = set_mask_cmd,     .argtable = &set_mask_args },
        { .command = "gw",       .help = "Set gateway IP",                .func = set_gw_cmd,       .argtable = &set_gw_args },
        { .command = "port",     .help = "Set listen port",               .func = set_port_cmd,     .argtable = &set_port_args },
        { .command = "sespass",  .help = "Set console session password",  .func = set_sespass_cmd,  .argtable = &set_sespass_args },
    };

    for (size_t i = 0; i < sizeof(cmds)/sizeof(cmds[0]); i++) {
        ESP_ERROR_CHECK(esp_console_cmd_register(&cmds[i]));
    }

    esp_console_register_help_command();
}

// ---------------- Initialization ----------------

void initialize_console_peripheral(void) {
    fflush(stdout);
    fsync(fileno(stdout));
    setvbuf(stdin, NULL, _IONBF, 0);

    uart_vfs_dev_port_set_rx_line_endings(CONFIG_ESP_CONSOLE_UART_NUM, ESP_LINE_ENDINGS_CR);
    uart_vfs_dev_port_set_tx_line_endings(CONFIG_ESP_CONSOLE_UART_NUM, ESP_LINE_ENDINGS_CRLF);

    uart_config_t uart_config = {
        .baud_rate  = CONFIG_ESP_CONSOLE_UART_BAUDRATE,
        .data_bits  = UART_DATA_8_BITS,
        .parity     = UART_PARITY_DISABLE,
        .stop_bits  = UART_STOP_BITS_1,
        .source_clk = UART_SCLK_XTAL,
    };

    ESP_ERROR_CHECK(uart_driver_install(CONFIG_ESP_CONSOLE_UART_NUM, 256, 0, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(CONFIG_ESP_CONSOLE_UART_NUM, &uart_config));
    uart_vfs_dev_use_driver(CONFIG_ESP_CONSOLE_UART_NUM);
}

void initialize_console_library(void) {
    esp_console_config_t console_config = {
        .max_cmdline_args = CONSOLE_MAX_CMDLINE_ARGS,
        .max_cmdline_length = CONSOLE_MAX_CMDLINE_LENGTH,
    };
    ESP_ERROR_CHECK(esp_console_init(&console_config));

    linenoiseSetMultiLine(1);
    linenoiseSetCompletionCallback(&esp_console_get_completion);
    linenoiseSetHintsCallback((linenoiseHintsCallback*)&esp_console_get_hint);
    linenoiseHistorySetMaxLen(100);
    linenoiseSetMaxLineLen(console_config.max_cmdline_length);
    linenoiseAllowEmpty(false);

    if (linenoiseProbe()) {
        ESP_LOGW(TAG, "Terminal lacks escape support. Line editing disabled.");
        linenoiseSetDumbMode(1);
    }
}

char *setup_prompt(const char *base) {
    const char *p = base ? base : "esp>";
    if (linenoiseIsDumbMode()) {
        snprintf(prompt, sizeof(prompt), "%s ", p);
    } else {
        snprintf(prompt, sizeof(prompt), LOG_COLOR_I "%s " LOG_RESET_COLOR, p);
    }
    return prompt;
}

void console_task(void *arg) {
    initialize_console_peripheral();
    initialize_console_library();
    console_register_commands();

    char *prompt_str = setup_prompt("esp>");

    printf("\n=== ESP32 Console Ready ===\n");
    if (!config_is_valid()) {
        conf_cmd(0, NULL);
        printf("\nPlease configure the device. Reboot when ready.\n\n");
    }
    printf("Type 'help' to see available commands.\n\n");

    if (linenoiseIsDumbMode()) {
        printf("\nDumb terminal. Line editing/history disabled.\n\n");
    }

    while (true) {
        char *line = linenoise(prompt_str);
        if (!line) continue;

        if (strlen(line) > 0) {
            linenoiseHistoryAdd(line);
        }

        int ret;
        esp_err_t err = esp_console_run(line, &ret);
        if (err == ESP_ERR_NOT_FOUND) {
            printf("Unrecognized command\n");
        } else if (err == ESP_ERR_INVALID_ARG) {
            // ignore empty
        } else if (err == ESP_OK && ret != ESP_OK) {
            printf("Command error: 0x%x (%s)\n", ret, esp_err_to_name(ret));
        } else if (err != ESP_OK) {
            printf("Internal error: %s\n", esp_err_to_name(err));
        }

        linenoiseFree(line);
    }

    esp_console_deinit();
    vTaskDelete(NULL);
}
