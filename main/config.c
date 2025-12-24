// config.c
#include "config.h"
#include "nvs_flash.h"
#include "nvs.h"
#include <string.h>
#include <stdio.h>
#include "esp_log.h"

static const char *TAG = "config";

device_config_t g_config = {0};

static esp_err_t nvs_open_write(nvs_handle_t *handle) {
    return nvs_open("config", NVS_READWRITE, handle);
}

// --- Save functions ---
esp_err_t config_save_ssid(const char *ssid) {
    strncpy(g_config.ssid, ssid, WIFI_SSID_MAX_LEN);
    g_config.ssid[WIFI_SSID_MAX_LEN] = '\0';

    nvs_handle_t handle;
    esp_err_t err = nvs_open_write(&handle);
    if (err != ESP_OK) return err;
    err = nvs_set_str(handle, "ssid", g_config.ssid);
    nvs_close(handle);
    return err;
}

esp_err_t config_save_password(const char *pass) {
    strncpy(g_config.password, pass, WIFI_PASS_MAX_LEN);
    g_config.password[WIFI_PASS_MAX_LEN] = '\0';

    nvs_handle_t handle;
    esp_err_t err = nvs_open_write(&handle);
    if (err != ESP_OK) return err;
    err = nvs_set_str(handle, "pass", g_config.password);
    nvs_close(handle);
    return err;
}

esp_err_t config_save_ip(const char *ip) {
    strncpy(g_config.ip, ip, IP_ADDR_STR_LEN - 1);
    g_config.ip[IP_ADDR_STR_LEN - 1] = '\0';

    nvs_handle_t handle;
    esp_err_t err = nvs_open_write(&handle);
    if (err != ESP_OK) return err;
    err = nvs_set_str(handle, "ip", g_config.ip);
    nvs_close(handle);
    return err;
}

esp_err_t config_save_netmask(const char *mask) {
    strncpy(g_config.netmask, mask, IP_ADDR_STR_LEN - 1);
    g_config.netmask[IP_ADDR_STR_LEN - 1] = '\0';

    nvs_handle_t handle;
    esp_err_t err = nvs_open_write(&handle);
    if (err != ESP_OK) return err;
    err = nvs_set_str(handle, "mask", g_config.netmask);
    nvs_close(handle);
    return err;
}

esp_err_t config_save_gateway(const char *gw) {
    strncpy(g_config.gateway, gw, IP_ADDR_STR_LEN - 1);
    g_config.gateway[IP_ADDR_STR_LEN - 1] = '\0';

    nvs_handle_t handle;
    esp_err_t err = nvs_open_write(&handle);
    if (err != ESP_OK) return err;
    err = nvs_set_str(handle, "gw", g_config.gateway);
    nvs_close(handle);
    return err;
}

esp_err_t config_save_port(uint16_t port) {
    g_config.port = (port == 0) ? 23 : port;

    nvs_handle_t handle;
    esp_err_t err = nvs_open_write(&handle);
    if (err != ESP_OK) return err;
    err = nvs_set_u16(handle, "port", g_config.port);
    nvs_close(handle);
    return err;
}

// --- Lifecycle ---
void config_reset_to_factory(void) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open("config", NVS_READWRITE, &handle);
    if (err == ESP_OK) {
        nvs_erase_all(handle);
        nvs_close(handle);
    }
    memset(&g_config, 0, sizeof(g_config));
    g_config.port = 23;
}

esp_err_t config_load(void) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open("config", NVS_READONLY, &handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "NVS empty or not initialized");
        return err;
    }

    size_t len;

    len = sizeof(g_config.ssid);
    nvs_get_str(handle, "ssid", g_config.ssid, &len);

    len = sizeof(g_config.password);
    nvs_get_str(handle, "pass", g_config.password, &len);

    len = sizeof(g_config.ip);
    nvs_get_str(handle, "ip", g_config.ip, &len);

    len = sizeof(g_config.netmask);
    nvs_get_str(handle, "mask", g_config.netmask, &len);

    len = sizeof(g_config.gateway);
    nvs_get_str(handle, "gw", g_config.gateway, &len);

    nvs_get_u16(handle, "port", &g_config.port);

    nvs_close(handle);

    if (g_config.port == 0) {
        g_config.port = 23;
    }

    return ESP_OK;
}

void config_init(void) {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGI(TAG, "NVS version mismatch, erasing...");
        nvs_flash_erase();
        ret = nvs_flash_init();
    }
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "NVS init failed (0x%x)", ret);
        config_reset_to_factory();
        return;
    }

    config_load();
}

bool config_is_valid(void) {
    return (g_config.ssid[0] != '\0') &&
           (g_config.password[0] != '\0') &&
           (g_config.ip[0] != '\0');
}