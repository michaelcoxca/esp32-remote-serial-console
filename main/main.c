// main.c
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "config.h"
#include "console.h"
#include "wifi_manager.h"
#include "tcp_server.h"
#include "usb_direct.h"
#include "ringbuf.h"
#include "usb_reader.h"

static const char* TAG = "main";

void app_main(void) {
    ESP_LOGI(TAG, "Running");

    //**********************************************
    // Provisioned or non-provisioned devices
    //**********************************************

    config_init();

    // Ring buffer and usb_reader to not block the USB Host
    ringbuf_t rb = ringbuf_create();
    if (!rb) {
        ESP_LOGE(TAG, "Failed to create ring buffer");
        return;
    }

    // Must have less priority than TCP server to facilitate sending the bytes
    if (xTaskCreate(usb_reader_task, "usb_reader", 4096, (void *)rb, 2, NULL) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create usb_reader task");
    }

    // Console task for provisioning
    if (xTaskCreate(console_task, "console", 4096, NULL, 3, NULL) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create console task");
    }

    if (!config_is_valid()) {
        ESP_LOGW(TAG, "No valid config - device unprovisioned");
        vTaskDelete(NULL);
        return;
    }
    
    //**********************************************
    // Only for provisioned devices
    //**********************************************    
    ESP_LOGI(TAG, "Valid config found - starting Wi-Fi and TCP server");
    
    wifi_manager_start();
    
    if (xTaskCreate(tcp_server_task, "tcp_server", 4096, (void *)rb, 1, NULL) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create tcp_server task");
    }

}










