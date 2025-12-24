// console.c
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "linenoise/linenoise.h"
#include "config.h"
#include "utils.h"

#define PROMPT "esp32> "

static void print_help(void) {
    printf("Posibles comandos:\n");
    printf("conf        -> muestra configuración actual\n");
    printf("reset       -> reinicio a valores de fábrica\n");
    printf("reboot      -> reinicia el programa\n");
    printf("set:\n");
    printf("  ssid <ssid> -> configura el AP wifi\n");
    printf("  pass <pass> -> configura la password\n");
    printf("  gw   <ip>   -> gateway (y DNS)\n");
    printf("  mask <mask> -> máscara de red\n");
    printf("  ip   <ip>   -> IP estática\n");
    printf("  port <port> -> puerto de escucha (default: 23)\n");
}

static void handle_conf(void) {
    printf("ssid: %s\n", g_config.ssid[0] ? g_config.ssid : "(not set)");
    printf("pass: %s\n", g_config.password[0] ? "*** (set)" : "(not set)");
    printf("ip:   %s\n", g_config.ip[0] ? g_config.ip : "(not set)");
    printf("mask: %s\n", g_config.netmask[0] ? g_config.netmask : "(not set)");
    printf("gw:   %s\n", g_config.gateway[0] ? g_config.gateway : "(not set)");
    printf("port: %d\n", g_config.port);
}

static void handle_set(char *cmd, char *arg) {
    esp_err_t err = ESP_FAIL;

    if (strcmp(cmd, "ssid") == 0) {
        if (!is_valid_ssid(arg)) {
            printf("ERR: SSID must be 1-32 chars\n");
            return;
        }
        err = config_save_ssid(arg);
    }
    else if (strcmp(cmd, "pass") == 0) {
        if (!is_valid_password(arg)) {
            printf("ERR: Password too long (max 64)\n");
            return;
        }
        err = config_save_password(arg);
    }
    else if (strcmp(cmd, "ip") == 0) {
        if (!is_valid_ipv4(arg)) {
            printf("ERR: Invalid IP address\n");
            return;
        }
        err = config_save_ip(arg);
    }
    else if (strcmp(cmd, "mask") == 0) {
        if (!is_valid_ipv4(arg)) {
            printf("ERR: Invalid netmask\n");
            return;
        }
        err = config_save_netmask(arg);
    }
    else if (strcmp(cmd, "gw") == 0) {
        if (!is_valid_ipv4(arg)) {
            printf("ERR: Invalid gateway IP\n");
            return;
        }
        err = config_save_gateway(arg);
    }
    else if (strcmp(cmd, "port") == 0) {
        uint16_t port;
        if (!is_valid_port(arg, &port)) {
            printf("ERR: Port must be 1-65535\n");
            return;
        }
        err = config_save_port(port);
    }
    else {
        printf("ERR: Unknown set command '%s'\n", cmd);
        return;
    }

    printf("%s\n", (err == ESP_OK) ? "OK" : "ERR: NVS write failed");
}

void console_task(void *pvParams) {
    char *line;
    (void)pvParams;

    // Optional: load history (not persistent by default in linenoise-ESP)
    linenoiseHistorySetMaxLen(10);

    printf("\n=== ESP32 UART Console ===\n");
    print_help();

    while (1) {
        line = linenoise(PROMPT);
        if (line == NULL) continue; // Ctrl-C/D

        if (strlen(line) > 0) {
            linenoiseHistoryAdd(line); // Add to history
            char *cmd = strtok(line, " \t");
            if (cmd) {
                if (strcmp(cmd, "help") == 0 || strcmp(cmd, "?") == 0) {
                    print_help();
                }
                else if (strcmp(cmd, "conf") == 0) {
                    handle_conf();
                }
                else if (strcmp(cmd, "reset") == 0) {
                    config_reset_to_factory();
                    printf("Factory reset done. Reboot to apply.\n");
                }
                else if (strcmp(cmd, "reboot") == 0) {
                    printf("Rebooting...\n");
                    esp_restart();
                }
                else if (strcmp(cmd, "set") == 0) {
                    char *subcmd = strtok(NULL, " \t");
                    char *arg = strtok(NULL, ""); // rest of line (to support spaces in SSID? optional)
                    if (!subcmd) {
                        printf("ERR: set requires subcommand (ssid, pass, ip, ...)\n");
                    } else if (!arg) {
                        printf("ERR: missing value for '%s'\n", subcmd);
                    } else {
                        // Trim leading space in arg (in case of "set ssid  MyNet")
                        while (*arg == ' ') arg++;
                        handle_set(subcmd, arg);
                    }
                } else {
                    printf("ERR: Unknown command. Type 'help'.\n");
                }
            }
        }
        linenoiseFree(line);
    }
}

void console_init(void) {
    xTaskCreate(console_task, "console", 4096, NULL, 5, NULL);
}