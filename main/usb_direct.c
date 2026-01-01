// main.c
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "hal/usb_serial_jtag_ll.h"


static const char* TAG = "usb";


// Direct USB JTAG write, no interrupts, no driver.
int usb_write(const uint8_t *buf, uint32_t len) {
    if (!buf || len == 0) return 0;

    uint32_t total_written = 0;
    const uint8_t *pos = buf;

    uint32_t timeouts = 0;
    const uint32_t MAX_TIMEOUTS = 100; // avoid infinite loop (e.g., if HW stuck)

    while (total_written < len) {
        int written_now = usb_serial_jtag_ll_write_txfifo(pos, len - total_written);
        usb_serial_jtag_ll_txfifo_flush();
        if (written_now > 0) {
            total_written += written_now;
            pos += written_now;
            timeouts = 0;
        } else {
            vTaskDelay(20 / portTICK_PERIOD_MS);  // 20 ms delay to retry
            if (++timeouts > MAX_TIMEOUTS) {
                ESP_LOGW(TAG, "TX timeout after %u retries", timeouts);
                break;
            }
        }
    }
    return (int)total_written;
}

// Direct USB JTAG read, no interrupts, no driver.
int usb_read(uint8_t *buf, uint32_t len) {
    int rx = usb_serial_jtag_ll_read_rxfifo(buf, len);
    if (rx > 0) {
        ESP_LOGI(TAG, "Rx %d bytes", rx);
        ESP_LOG_BUFFER_HEXDUMP("USB RX", buf, rx, ESP_LOG_INFO);
    }
    return rx;
}


















