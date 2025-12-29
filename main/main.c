// main.c
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "esp_vfs_dev.h"
#include "esp_log.h"
#include "config.h"
#include "console.h"
#include "wifi_manager.h"


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
        wifi_manager_start();
    }
    else {
        ESP_LOGW(TAG, "Device is not configured yet.");
    }

    // Keep main task alive (e.g., for background work)
    while (1) {
        if (wifi_manager_is_connected()) {
            ESP_LOGI(TAG, "WiFi up");
        }
        else {
            ESP_LOGI(TAG, "WiFi down");
        }
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}



















