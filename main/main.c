// main.c
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "esp_vfs_dev.h"
#include "esp_log.h"
#include "esp_console.h"
#include "linenoise/linenoise.h"
#include "config.h"
#include "console.h"
#include "console_settings.h"

static const char* TAG = "main";
#define PROMPT_STR "esp"


static void console_task(void *arg) {
    initialize_console_peripheral();
    initialize_console_library();
    
    // Register all commands (defined in console.c)
    console_register_commands();

    /* Set up prompt */
    const char* prompt = setup_prompt(PROMPT_STR ">");

    printf("\n=== ESP32 Console Ready ===\n");
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


void app_main(void) {
    // --- 1. Initialize NVS and config ---
    config_init();

    // --- 3. Start console in a dedicated task ---
    xTaskCreate(console_task, "console", 6144, NULL, tskIDLE_PRIORITY + 3, NULL);

    // --- 4. app_main() can now do other work (or sleep) ---
    // Example: start Wi-Fi if config is valid
    if (config_is_valid()) {
        printf("Config valid. Starting application...\n");
        // wifi_start_static();
    }

    // Keep main task alive (e.g., for background work)
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(5000)); // or do real work
    }
}










/*
#include <stdio.h>
#include <string.h>
#include "esp_system.h"
#include "esp_vfs_fat.h"
#include "argtable3/argtable3.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "cmd_nvs.h"
#include "cmd_system.h"
#include "console_settings.h"
*/

















