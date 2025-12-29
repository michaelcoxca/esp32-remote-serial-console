// wifi_manager.c
#include "wifi_manager.h"
#include "config.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include <string.h>

static const char *TAG = "wifi_manager";

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static EventGroupHandle_t s_wifi_event_group;
static int s_retry_num = 0;
static bool s_connected = false;
static esp_netif_t *sta_netif = NULL;

// Forward declarations
static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data);

void wifi_manager_stop(void) {
    esp_wifi_stop();
    s_connected = false;
    s_retry_num = 0;
    ESP_LOGI(TAG, "Wi-Fi stopped");
}

bool wifi_manager_is_connected(void) {
    return s_connected;
}

void wifi_manager_start(void) {
    // Take a snapshot of current config (thread-safe)
    device_config_t cfg;
    memcpy(&cfg, &g_config, sizeof(device_config_t));

    // Validate
    if (cfg.ssid[0] == '\0' || cfg.password[0] == '\0' || cfg.ip[0] == '\0') {
        ESP_LOGW(TAG, "Wi-Fi config incomplete — skipping connection");
        return;
    }

    ESP_LOGI(TAG, "Wi-Fi config:");
    ESP_LOGI(TAG, "  SSID: '%s'", cfg.ssid);
    ESP_LOGI(TAG, "  Password: '%s'", cfg.password[0] ? "***" : "(empty)");
    ESP_LOGI(TAG, "  Static IP: %s", cfg.ip);
    ESP_LOGI(TAG, "  Netmask: %s", cfg.netmask);
    ESP_LOGI(TAG, "  Gateway: %s", cfg.gateway);
    ESP_LOGI(TAG, "  Port: %d", cfg.port);

    // Initialize TCP/IP network interface (should be called only once)
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // Create default station netif
    sta_netif = esp_netif_create_default_wifi_sta();
    assert(sta_netif);

    wifi_init_config_t cfg_wifi = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg_wifi));

    // Register event handler
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        NULL));

    // Prepare Wi-Fi config
    wifi_config_t wifi_config = {0};
    strlcpy((char *)wifi_config.sta.ssid, cfg.ssid, sizeof(wifi_config.sta.ssid));
    strlcpy((char *)wifi_config.sta.password, cfg.password, sizeof(wifi_config.sta.password));
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    wifi_config.sta.pmf_cfg.capable = true;
    wifi_config.sta.pmf_cfg.required = false;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));

    // --- Configure static IP ---
    esp_netif_ip_info_t ip_info = {0};
    
    uint32_t ip = esp_ip4addr_aton(cfg.ip);
    if (ip == 0) {
        ESP_LOGE(TAG, "Invalid static IP: %s", cfg.ip);
        return;
    }
    ip_info.ip.addr = ip;
    
    uint32_t mask = esp_ip4addr_aton(cfg.netmask);
    if (mask == 0) {
        ESP_LOGE(TAG, "Invalid netmask: %s", cfg.netmask);
        return;
    }
    ip_info.netmask.addr = mask;
    
    uint32_t gw = esp_ip4addr_aton(cfg.gateway);
    if (gw == 0) {
        ESP_LOGE(TAG, "Invalid gateway: %s", cfg.gateway);
        return;
    }
    ip_info.gw.addr = gw;
    
    if (esp_netif_dhcpc_stop(sta_netif) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to stop dhcp client");
        return;
    }

    ESP_ERROR_CHECK(esp_netif_set_ip_info(sta_netif, &ip_info));

    ESP_LOGI(TAG, "Connecting to Wi-Fi: ssid='%s', ip=%s, gw=%s, mask=%s",
             cfg.ssid, cfg.ip, cfg.gateway, cfg.netmask);

    s_wifi_event_group = xEventGroupCreate();
    s_connected = false;
    s_retry_num = 0;

    ESP_ERROR_CHECK(esp_wifi_start());
    

}


// Helper to convert reason codes to text
static const char* wifi_disconnect_reason_str(wifi_event_sta_disconnected_t *disconn) {
    switch (disconn->reason) {
        case WIFI_REASON_UNSPECIFIED:
            return "Unspecified reason";
        case WIFI_REASON_AUTH_EXPIRE:
            return "Authentication expired";
        case WIFI_REASON_AUTH_LEAVE:
            return "Deauthentication due to leaving";
        case WIFI_REASON_DISASSOC_DUE_TO_INACTIVITY:  // Same as WIFI_REASON_ASSOC_EXPIRE (deprecated)
            return "Disassociated due to inactivity";
        case WIFI_REASON_ASSOC_TOOMANY:
            return "Too many associated stations";
        case WIFI_REASON_CLASS2_FRAME_FROM_NONAUTH_STA:  // Same as WIFI_REASON_NOT_AUTHED (deprecated)
            return "Class 2 frame received from nonauthenticated STA";
        case WIFI_REASON_CLASS3_FRAME_FROM_NONASSOC_STA:  // Same as WIFI_REASON_NOT_ASSOCED (deprecated)
            return "Class 3 frame received from nonassociated STA";
        case WIFI_REASON_ASSOC_LEAVE:
            return "Deassociated due to leaving";
        case WIFI_REASON_ASSOC_NOT_AUTHED:
            return "Association but not authenticated";
        case WIFI_REASON_DISASSOC_PWRCAP_BAD:
            return "Disassociated due to poor power capability";
        case WIFI_REASON_DISASSOC_SUPCHAN_BAD:
            return "Disassociated due to unsupported channel";
        case WIFI_REASON_BSS_TRANSITION_DISASSOC:
            return "Disassociated due to BSS transition";
        case WIFI_REASON_IE_INVALID:
            return "Invalid Information Element (IE)";
        case WIFI_REASON_MIC_FAILURE:
            return "MIC failure";
        case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
            return "4-way handshake timeout";
        case WIFI_REASON_GROUP_KEY_UPDATE_TIMEOUT:
            return "Group key update timeout";
        case WIFI_REASON_IE_IN_4WAY_DIFFERS:
            return "IE differs in 4-way handshake";
        case WIFI_REASON_GROUP_CIPHER_INVALID:
            return "Invalid group cipher";
        case WIFI_REASON_PAIRWISE_CIPHER_INVALID:
            return "Invalid pairwise cipher";
        case WIFI_REASON_AKMP_INVALID:
            return "Invalid AKMP";
        case WIFI_REASON_UNSUPP_RSN_IE_VERSION:
            return "Unsupported RSN IE version";
        case WIFI_REASON_INVALID_RSN_IE_CAP:
            return "Invalid RSN IE capabilities";
        case WIFI_REASON_802_1X_AUTH_FAILED:
            return "802.1X authentication failed";
        case WIFI_REASON_CIPHER_SUITE_REJECTED:
            return "Cipher suite rejected";
        case WIFI_REASON_TDLS_PEER_UNREACHABLE:
            return "TDLS peer unreachable";
        case WIFI_REASON_TDLS_UNSPECIFIED:
            return "TDLS unspecified";
        case WIFI_REASON_SSP_REQUESTED_DISASSOC:
            return "SSP requested disassociation";
        case WIFI_REASON_NO_SSP_ROAMING_AGREEMENT:
            return "No SSP roaming agreement";
        case WIFI_REASON_BAD_CIPHER_OR_AKM:
            return "Bad cipher or AKM";
        case WIFI_REASON_NOT_AUTHORIZED_THIS_LOCATION:
            return "Not authorized in this location";
        case WIFI_REASON_SERVICE_CHANGE_PERCLUDES_TS:
            return "Service change precludes TS";
        case WIFI_REASON_UNSPECIFIED_QOS:
            return "Unspecified QoS reason";
        case WIFI_REASON_NOT_ENOUGH_BANDWIDTH:
            return "Not enough bandwidth";
        case WIFI_REASON_MISSING_ACKS:
            return "Missing ACKs";
        case WIFI_REASON_EXCEEDED_TXOP:
            return "Exceeded TXOP";
        case WIFI_REASON_STA_LEAVING:
            return "Station leaving";
        case WIFI_REASON_END_BA:
            return "End of Block Ack (BA)";
        case WIFI_REASON_UNKNOWN_BA:
            return "Unknown Block Ack (BA)";
        case WIFI_REASON_TIMEOUT:
            return "Timeout";
        case WIFI_REASON_PEER_INITIATED:
            return "Peer initiated disassociation";
        case WIFI_REASON_AP_INITIATED:
            return "AP initiated disassociation";
        case WIFI_REASON_INVALID_FT_ACTION_FRAME_COUNT:
            return "Invalid FT action frame count";
        case WIFI_REASON_INVALID_PMKID:
            return "Invalid PMKID";
        case WIFI_REASON_INVALID_MDE:
            return "Invalid MDE";
        case WIFI_REASON_INVALID_FTE:
            return "Invalid FTE";
        case WIFI_REASON_TRANSMISSION_LINK_ESTABLISH_FAILED:
            return "Transmission link establishment failed";
        case WIFI_REASON_ALTERATIVE_CHANNEL_OCCUPIED:
            return "Alternative channel occupied";
        case WIFI_REASON_BEACON_TIMEOUT:
            return "Beacon timeout";
        case WIFI_REASON_NO_AP_FOUND:
            return "No AP found";
        case WIFI_REASON_AUTH_FAIL:
            return "Authentication failed";
        case WIFI_REASON_ASSOC_FAIL:
            return "Association failed";
        case WIFI_REASON_HANDSHAKE_TIMEOUT:
            return "Handshake timeout";
        case WIFI_REASON_CONNECTION_FAIL:
            return "Connection failed";
        case WIFI_REASON_AP_TSF_RESET:
            return "AP TSF reset";
        case WIFI_REASON_ROAMING:
            return "Roaming";
        case WIFI_REASON_ASSOC_COMEBACK_TIME_TOO_LONG:
            return "Association comeback time too long";
        case WIFI_REASON_SA_QUERY_TIMEOUT:
            return "SA query timeout";
        case WIFI_REASON_NO_AP_FOUND_W_COMPATIBLE_SECURITY:
            return "No AP found with compatible security";
        case WIFI_REASON_NO_AP_FOUND_IN_AUTHMODE_THRESHOLD:
            return "No AP found in auth mode threshold";
        case WIFI_REASON_NO_AP_FOUND_IN_RSSI_THRESHOLD:
            return "No AP found in RSSI threshold";
        default:
            return "Unknown WiFi error reason";
    }
}

static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT) {
        switch (event_id) {
            case WIFI_EVENT_STA_START:
                ESP_LOGI(TAG, "Wi-Fi STA started");
                esp_wifi_connect();
                break;

            case WIFI_EVENT_STA_STOP:
                ESP_LOGI(TAG, "Wi-Fi STA stopped");
                break;

            case WIFI_EVENT_STA_CONNECTED:
                ESP_LOGI(TAG, "Wi-Fi CONNECTED to AP");
                break;

            case WIFI_EVENT_STA_DISCONNECTED: {
                wifi_event_sta_disconnected_t* event = (wifi_event_sta_disconnected_t*) event_data;
                ESP_LOGW(TAG, "Wi-Fi DISCONNECTED (reason: %d - %s)", 
                         event->reason, wifi_disconnect_reason_str(event));
                
                s_connected = false;
                
                if (s_retry_num < 3) {
                    ESP_LOGI(TAG, "Retrying connection (%d) immediately...", s_retry_num + 1);
                    vTaskDelay(pdMS_TO_TICKS(1000));
                    esp_wifi_connect();
                    s_retry_num++;
                } else if (s_retry_num < 10) {
                    ESP_LOGI(TAG, "Retrying connection (%d) after 10 secs...", s_retry_num + 1);
                    vTaskDelay(pdMS_TO_TICKS(10000));
                    esp_wifi_connect();
                    s_retry_num++;
                } else {
                    ESP_LOGI(TAG, "Retrying connection after 300 secs...");
                    vTaskDelay(pdMS_TO_TICKS(300000));
                    esp_wifi_connect();
                    //ESP_LOGE(TAG, "Failed to connect after 5 attempts");
                    //xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
                }
                break;
            }

            case WIFI_EVENT_STA_AUTHMODE_CHANGE: {
                wifi_event_sta_authmode_change_t* event = (wifi_event_sta_authmode_change_t*) event_data;
                ESP_LOGI(TAG, "Auth mode changed from %d to %d", event->old_mode, event->new_mode);
                break;
            }

            default:
                ESP_LOGD(TAG, "Unhandled Wi-Fi event: %d", (int)event_id);
                break;
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        s_connected = true;
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    } else {
        ESP_LOGD(TAG, "Unhandled event: %s %d", event_base, (int)event_id);
    }
}