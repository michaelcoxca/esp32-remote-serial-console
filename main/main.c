// main.c
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "config.h"
#include "console.h"
#include "wifi_manager.h"
#include "tcp_server.h"


static const char* TAG = "main";

void app_main(void) {
    ESP_LOGI(TAG, "Running");

    config_init();

    if (xTaskCreate(console_task, "console", 8192, NULL, 1, NULL) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create console task");
    }

    if (config_is_valid()) {
        ESP_LOGI(TAG, "Valid config found - starting Wi-Fi and TCP server");
        wifi_manager_start();
        tcp_server_start();
    } else {
        ESP_LOGW(TAG, "No valid config - device unprovisioned");
    }

    vTaskDelete(NULL);
}










