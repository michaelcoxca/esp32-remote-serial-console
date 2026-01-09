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

    ESP_LOGI(TAG, "USB reader task started");

    while (1) {
        int rx = usb_read(buf, sizeof(buf));

        if (rx > 0) {
            // Push all received bytes into ring buffer
            size_t written = ringbuf_put_bytes(rb, buf, (size_t)rx);
            if (written != (size_t)rx) {
                // This shouldn't happen with overwrite-on-full design,
                // but log just in case
                ESP_LOGD(TAG, "Ring buffer dropped %d bytes", (int)(rx - written));
            }
        }
        // Small delay to avoid tight loop since usb_read() is non-blocking
        // Also to give time for TCP server to send the bytes.
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
