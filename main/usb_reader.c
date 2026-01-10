// usb_reader.c - Background task that reads USB and writes to ring buffer

#include "usb_reader.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "ringbuf.h"
#include "usb_direct.h"

static const char* TAG = "usb_reader";

#define USB_READ_CHUNK_SIZE 64

void usb_reader_task(void* pvParameters) {
    ringbuf_t rb = (ringbuf_t)pvParameters;
    
    if (!rb) {
        ESP_LOGE(TAG, "Ring buffer is NULL!");
        vTaskDelete(NULL);
        return;
    }
    
    uint8_t buf[USB_READ_CHUNK_SIZE];
    const TickType_t MAX_TIME_BETWEEN_PAUSE = pdMS_TO_TICKS(50); // 50ms

    TickType_t last_pause_time = xTaskGetTickCount();

    ESP_LOGI(TAG, "USB reader task started");

    while (1) {
        int rx = usb_read(buf, sizeof(buf));
        size_t written = 0;

        if (rx > 0) {
            written = ringbuf_put_bytes(rb, buf, (size_t)rx);

            if (written != (size_t)rx) {
                ESP_LOGD(TAG, "Dropped %d bytes", (int)(rx - written));
            }
        }

        // Compute 3/4 fill threshold
        uint32_t count = ringbuf_count(rb);
        uint32_t capacity = ringbuf_capacity(rb);
        uint32_t threshold_3_4 = (3 * capacity) / 4;

        // If buffer is >= 3/4 full, slow down
        if (count >= threshold_3_4) {
            vTaskDelay(pdMS_TO_TICKS(15));
            last_pause_time = xTaskGetTickCount();
            continue;
        }

        TickType_t now = xTaskGetTickCount();
        TickType_t time_elapsed = now - last_pause_time;

        if (time_elapsed >= MAX_TIME_BETWEEN_PAUSE) {
            vTaskDelay(1); // minimal yield for watchdog
            last_pause_time = xTaskGetTickCount();
        }

    }
}
