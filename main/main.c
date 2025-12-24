// main.c
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "esp_vfs_dev.h"
#include "esp_log.h"
#include "config.h"
#include "console.h"


static const char* TAG = "main";



void app_main(void) {
    ESP_LOGI(TAG, "Running");

    // --- 1. Initialize NVS and config ---
    config_init();

    // --- 3. Start console in a dedicated task ---
    xTaskCreate(console_task, "console", 6144, NULL, tskIDLE_PRIORITY + 3, NULL);

    // --- 4. app_main() can now do other work (or sleep) ---
    // Example: start Wi-Fi if config is valid
    if (config_is_valid()) {
        ESP_LOGI(TAG, "Config is valid: starting WiFi");
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

















