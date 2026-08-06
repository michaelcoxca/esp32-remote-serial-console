set(srcs  "main.c"
          "config.c"
          "console.c"
          "tcp_server.c"
          "usb_direct.c"
          "ringbuf.c"
          "usb_reader.c")

set (requires "console"
          "nvs_flash"
          "vfs"
          "esp_driver_uart"
          "esp_wifi"
          "esp_netif" 
          "esp_event"
          "esp_timer"
          "esp_driver_gpio")

if (CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG)
     list(APPEND srcs "esp_driver_usb_serial_jtag")
endif()

if(CONFIG_WIFI_MODE_STA_SELECTED)
    list(APPEND srcs "wifi_manager.c")
endif()

idf_component_register(
    SRCS ${srcs}
    INCLUDE_DIRS "."
    REQUIRES ${requires}
)