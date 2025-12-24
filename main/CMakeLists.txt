idf_component_register(SRCS "main.c" "config.c" "console.c" "utils.c" "console_settings.c"
                       INCLUDE_DIRS "."
                       REQUIRES console nvs_flash vfs esp_driver_uart)