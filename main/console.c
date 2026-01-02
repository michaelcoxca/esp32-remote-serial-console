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
#include "console_settings.h"

static int conf_cmd(int argc, char **argv);
static int reboot_cmd(int argc, char **argv);

static const char* TAG = "console";

#define PROMPT_STR "esp"

// --- Argument structs for each command ---
static struct {
    struct arg_str *ssid;
    struct arg_end *end;
} set_ssid_args;

static struct {
    struct arg_str *pass;
    struct arg_end *end;
} set_pass_args;

static struct {
    struct arg_str *ip;
    struct arg_end *end;
} set_ip_args;

static struct {
    struct arg_str *mask;
    struct arg_end *end;
} set_mask_args;

static struct {
    struct arg_str *gw;
    struct arg_end *end;
} set_gw_args;

static struct {
    struct arg_int *port;
    struct arg_end *end;
} set_port_args;

static struct {
    struct arg_str *pass;
    struct arg_end *end;
} set_sespass_args;

// --- Command handlers ---
static int conf_cmd(int argc, char **argv) {
    // Reload config from NVS to show true persistent state
    device_config_t temp_config;
    memcpy(&temp_config, &g_config, sizeof(device_config_t)); // backup current RAM state

    esp_err_t err = config_load(); // reload from NVS into g_config
    if (err != ESP_OK) {
        printf("Warning: Failed to reload config from NVS (using current RAM copy)\n");
        // Optionally restore backup if you don't want g_config changed
        // But usually, it's safe to keep the loaded state
    }

    bool is_configured = config_is_valid(); // This checks: ssid && password && ip

    printf("=== Configuration Status ===\n");
    if (is_configured) {
        printf("Device is FULLY CONFIGURED and ready to connect.\n");
    } else {
        printf("Device is INCOMPLETELY CONFIGURED.\n");
    }
    printf("\n");

    printf("=== Configuration Values ===\n");    
    printf("ssid:    %s\n", g_config.ssid[0] ? g_config.ssid : "(not set)");
    printf("pass:    %s\n", g_config.password[0] ? "*** (set)" : "(not set)");
    printf("ip:      %s\n", g_config.ip[0] ? g_config.ip : "(not set)");
    printf("mask:    %s\n", g_config.netmask[0] ? g_config.netmask : "(not set)");
    printf("gw:      %s\n", g_config.gateway[0] ? g_config.gateway : "(not set)");
    printf("port:    %d\n", g_config.port);
    printf("sespass: %s\n", g_config.sespass[0] ? "*** (set)" : "(not set)");
    return 0;
}

static int reset_cmd(int argc, char **argv) {
    config_reset_to_factory();
    printf("Factory reset done. Rebooting to apply.\n");
    reboot_cmd(0, NULL);
    return 0;
}

static int reboot_cmd(int argc, char **argv) {
    printf("Rebooting...\n");
    fflush(stdout);
    vTaskDelay(pdMS_TO_TICKS(100));
    esp_restart();
    return 0;
}

// --- SET subcommands ---
static int set_ssid_cmd(int argc, char **argv) {
    int nerrors = arg_parse(argc, argv, (void **)&set_ssid_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, set_ssid_args.end, argv[0]);
        return 1;
    }

    const char *ssid = set_ssid_args.ssid->sval[0];
    if (!is_valid_ssid(ssid)) {
        printf("ERR: SSID must be 1-32 chars\n");
        return 1;
    }

    esp_err_t err = config_save_ssid(ssid);
    if (err != ESP_OK) {
        printf("ERR: NVS write failed (0x%x)\n", err);
        return 1;
    }
    printf("OK\n");
    return 0;
}

static int set_pass_cmd(int argc, char **argv) {
    int nerrors = arg_parse(argc, argv, (void **)&set_pass_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, set_pass_args.end, argv[0]);
        return 1;
    }

    const char *pass = set_pass_args.pass->sval[0];
    if (!is_valid_password(pass)) {
        printf("ERR: Password too long (max 64 chars)\n");
        return 1;
    }

    esp_err_t err = config_save_password(pass);
    if (err != ESP_OK) {
        printf("ERR: NVS write failed\n");
        return 1;
    }
    printf("OK\n");
    return 0;
}

static int set_ip_cmd(int argc, char **argv) {
    int nerrors = arg_parse(argc, argv, (void **)&set_ip_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, set_ip_args.end, argv[0]);
        return 1;
    }

    const char *ip = set_ip_args.ip->sval[0];
    if (!is_valid_ipv4(ip)) {
        printf("ERR: Invalid IP address\n");
        return 1;
    }

    esp_err_t err = config_save_ip(ip);
    if (err != ESP_OK) {
        printf("ERR: NVS write failed\n");
        return 1;
    }
    printf("OK\n");
    return 0;
}

static int set_mask_cmd(int argc, char **argv) {
    int nerrors = arg_parse(argc, argv, (void **)&set_mask_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, set_mask_args.end, argv[0]);
        return 1;
    }

    const char *mask = set_mask_args.mask->sval[0];
    if (!is_valid_ipv4(mask)) {
        printf("ERR: Invalid netmask\n");
        return 1;
    }

    esp_err_t err = config_save_netmask(mask);
    if (err != ESP_OK) {
        printf("ERR: NVS write failed\n");
        return 1;
    }
    printf("OK\n");
    return 0;
}

static int set_gw_cmd(int argc, char **argv) {
    int nerrors = arg_parse(argc, argv, (void **)&set_gw_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, set_gw_args.end, argv[0]);
        return 1;
    }

    const char *gw = set_gw_args.gw->sval[0];
    if (!is_valid_ipv4(gw)) {
        printf("ERR: Invalid gateway IP\n");
        return 1;
    }

    esp_err_t err = config_save_gateway(gw);
    if (err != ESP_OK) {
        printf("ERR: NVS write failed\n");
        return 1;
    }
    printf("OK\n");
    return 0;
}

static int set_port_cmd(int argc, char **argv) {
    int nerrors = arg_parse(argc, argv, (void **)&set_port_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, set_port_args.end, argv[0]);
        return 1;
    }

    int port = set_port_args.port->ival[0];
    if (port < 1 || port > 65535) {
        printf("ERR: Port must be 1-65535\n");
        return 1;
    }

    esp_err_t err = config_save_port((uint16_t)port);
    if (err != ESP_OK) {
        printf("ERR: NVS write failed\n");
        return 1;
    }
    printf("OK\n");
    return 0;
}

static int set_sespass_cmd(int argc, char **argv) {
    int nerrors = arg_parse(argc, argv, (void **)&set_sespass_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, set_sespass_args.end, argv[0]);
        return 1;
    }

    const char *pass = set_sespass_args.pass->sval[0];
    if (!is_valid_sespass(pass)) {
        printf("ERR: Password too long (max 64 chars)\n");
        return 1;
    }

    esp_err_t err = config_save_sespass(pass);
    if (err != ESP_OK) {
        printf("ERR: NVS write failed\n");
        return 1;
    }
    printf("OK\n");
    return 0;
}

// --- Register all commands ---
void console_register_commands(void) {
    // Register commands
    const esp_console_cmd_t conf_cmd_cfg = {
        .command = "conf",
        .help = "Show current configuration",
        .func = &conf_cmd,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&conf_cmd_cfg));

    const esp_console_cmd_t reset_cmd_cfg = {
        .command = "reset",
        .help = "Reset to factory defaults",
        .func = &reset_cmd,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&reset_cmd_cfg));

    const esp_console_cmd_t reboot_cmd_cfg = {
        .command = "reboot",
        .help = "Reboot the device",
        .func = &reboot_cmd,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&reboot_cmd_cfg));

    // SET commands
    set_ssid_args.ssid = arg_str1(NULL, NULL, "<ssid>", "WiFi SSID (1-32 chars)");
    set_ssid_args.end = arg_end(1);
    const esp_console_cmd_t set_ssid_cmd_cfg = {
        .command = "ssid",
        .help = "Set WiFi SSID",
        .func = &set_ssid_cmd,
        .argtable = &set_ssid_args
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&set_ssid_cmd_cfg));

    set_pass_args.pass = arg_str1(NULL, NULL, "<pass>", "WiFi password (0-64 chars)");
    set_pass_args.end = arg_end(1);
    const esp_console_cmd_t set_pass_cmd_cfg = {
        .command = "pass",
        .help = "Set WiFi password",
        .func = &set_pass_cmd,
        .argtable = &set_pass_args
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&set_pass_cmd_cfg));

    set_ip_args.ip = arg_str1(NULL, NULL, "<ip>", "Static IP address");
    set_ip_args.end = arg_end(1);
    const esp_console_cmd_t set_ip_cmd_cfg = {
        .command = "ip",
        .help = "Set static IP",
        .func = &set_ip_cmd,
        .argtable = &set_ip_args
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&set_ip_cmd_cfg));

    set_mask_args.mask = arg_str1(NULL, NULL, "<mask>", "Network mask");
    set_mask_args.end = arg_end(1);
    const esp_console_cmd_t set_mask_cmd_cfg = {
        .command = "mask",
        .help = "Set network mask",
        .func = &set_mask_cmd,
        .argtable = &set_mask_args
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&set_mask_cmd_cfg));

    set_gw_args.gw = arg_str1(NULL, NULL, "<gw>", "Gateway IP");
    set_gw_args.end = arg_end(1);
    const esp_console_cmd_t set_gw_cmd_cfg = {
        .command = "gw",
        .help = "Set gateway IP",
        .func = &set_gw_cmd,
        .argtable = &set_gw_args
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&set_gw_cmd_cfg));

    set_port_args.port = arg_int1(NULL, NULL, "<port>", "Listen port (1-65535)");
    set_port_args.end = arg_end(1);
    const esp_console_cmd_t set_port_cmd_cfg = {
        .command = "port",
        .help = "Set listen port",
        .func = &set_port_cmd,
        .argtable = &set_port_args
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&set_port_cmd_cfg));

    set_sespass_args.pass = arg_str1(NULL, NULL, "<password>", "Console session password (0-64 chars, empty to disable)");
    set_sespass_args.end = arg_end(1);
    const esp_console_cmd_t set_sespass_cmd_cfg = {
        .command = "sespass",
        .help = "Set console session password",
        .func = &set_sespass_cmd,
        .argtable = &set_sespass_args
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&set_sespass_cmd_cfg));


    // Force register help
    esp_console_register_help_command();
}

// Main console task
void console_task(void *arg) {
    initialize_console_peripheral();
    initialize_console_library();
    
    // Register all commands (defined in console.c)
    console_register_commands();

    /* Set up prompt */
    const char* prompt = setup_prompt(PROMPT_STR ">");

    printf("\n=== ESP32 Console Ready ===\n");
    if (!config_is_valid()) {
        conf_cmd(0, NULL);
        printf("\nPlease configure the device. Reboot when ready.\n\n");
    }
    printf("Type 'help' to see available commands.\n\n");

    if (linenoiseIsDumbMode()) {
        printf("\n"
               "Your terminal application does not support escape sequences.\n"
               "Line editing and history features are disabled.\n"
               "On Windows, try using Windows Terminal or Putty instead.\n\n");
    }

    // REPL loop (runs forever in this task)
    /* Main loop */
    while(true) {
        char* line = linenoise(prompt);
        if (line == NULL) {
            continue;
        }

        /* Add the command to the history if not empty */
        if (strlen(line) > 0) {
            linenoiseHistoryAdd(line);
        }

        /* Try to run the command */
        int ret;
        esp_err_t err = esp_console_run(line, &ret);
        if (err == ESP_ERR_NOT_FOUND) {
            printf("Unrecognized command\n");
        } else if (err == ESP_ERR_INVALID_ARG) {
            // command was empty
        } else if (err == ESP_OK && ret != ESP_OK) {
            printf("Command returned non-zero error code: 0x%x (%s)\n", ret, esp_err_to_name(ret));
        } else if (err != ESP_OK) {
            printf("Internal error: %s\n", esp_err_to_name(err));
        }

        linenoiseFree(line);
    }

    ESP_LOGE(TAG, "Error or end-of-input, terminating console");
    esp_console_deinit();
}
