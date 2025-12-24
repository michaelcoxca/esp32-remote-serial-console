// main.c
#include "config.h"
#include "console.h"
#include "esp_log.h"
#include "driver/uart.h"
#include "esp_vfs_dev.h"

static const char* TAG = "remotecon";

void app_main(void) {
    // Initialize UART0 for console
    uart_config_t uart_config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE
    };
    uart_param_config(UART_NUM_0, &uart_config);
    uart_driver_install(UART_NUM_0, 256, 0, 0, NULL, 0);
    
    // Redirect stdin/stdout to UART0
    esp_vfs_dev_uart_use_driver(UART_NUM_0);
    esp_vfs_dev_uart_set_rx_line_endings(ESP_LINE_ENDINGS_LF);
    esp_vfs_dev_uart_set_tx_line_endings(ESP_LINE_ENDINGS_LF);

    // Now safe to use printf and linenoise
    config_init();
    console_init();

    // CRITICAL: Yield or sleep in main, or watchdog will trigger
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000)); // or do real work
    }
}