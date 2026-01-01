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
#include "tcp_server.h"


static const char* TAG = "main";


void app_main(void) {
    ESP_LOGI(TAG, "Running");

    // Initialize NVS and config
    config_init();
    xTaskCreate(console_task, "console", 6144, NULL, tskIDLE_PRIORITY + 3, NULL);

    // Start Wi-Fi if config is valid
    if (config_is_valid()) {
        ESP_LOGI(TAG, "Config is valid: starting console server");
        wifi_manager_start();
        tcp_server_start();
    }
    else {
        ESP_LOGW(TAG, "Device is not configured yet.");
    }

    // Keep main task alive (e.g., for background work)
    while (1) {
        //ESP_LOGI(TAG, "Idle");
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}



















