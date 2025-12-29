// config.h
#ifndef CONFIG_H
#define CONFIG_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#define WIFI_SSID_MAX_LEN   32
#define WIFI_PASS_MAX_LEN   64
#define IP_ADDR_STR_LEN     16  // "255.255.255.255"

typedef struct {
    char ssid[WIFI_SSID_MAX_LEN + 1];
    char password[WIFI_PASS_MAX_LEN + 1];
    char ip[IP_ADDR_STR_LEN];
    char netmask[IP_ADDR_STR_LEN];
    char gateway[IP_ADDR_STR_LEN];
    uint16_t port;
} device_config_t;

extern device_config_t g_config;

// Public API
void config_init(void);
void config_reset_to_factory(void);
bool config_is_valid(void);
esp_err_t config_load(void);
bool is_valid_ipv4(const char *ip);
bool is_valid_ssid(const char *ssid);
bool is_valid_password(const char *pass);

// Fine-grained save functions (validate before calling!)
esp_err_t config_save_ssid(const char *ssid);
esp_err_t config_save_password(const char *pass);
esp_err_t config_save_ip(const char *ip);
esp_err_t config_save_netmask(const char *mask);
esp_err_t config_save_gateway(const char *gw);
esp_err_t config_save_port(uint16_t port);
#endif // CONFIG_H