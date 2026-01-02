#include <string.h>
#include <ctype.h>
#include <sys/param.h>
#include <sys/time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_timer.h"


#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include <lwip/netdb.h>

#include "config.h"
#include "usb_direct.h"


// Telnet protocol constants
#define TELNET_IAC       255
#define TELNET_WILL      251
#define TELNET_WONT      252
#define TELNET_DO        253
#define TELNET_DONT      254
#define TELNET_SB        242
#define TELNET_SE        240
#define TELNET_EOF       236
#define TELNET_ECHO      1
#define TELNET_SGA       3

#define TELNET_CR        13
#define TELNET_LF        10

#define USB_BUF_SIZE     64    // we will read/write in 64 bytes chunks

#define KEEPALIVE_IDLE     5    // Start probing after X seconds of inactivity
#define KEEPALIVE_INTERVAL 1    // Send a probe every X seconds
#define KEEPALIVE_COUNT    5    // Drop connection after X failed probes
#define NODELAY_FLAG       1    // Disable Nagle’s algorithm

#define MAX_ATTEMPTS 3
#define LOCK_TIME_SEC 60  // 1 minute


static const char *TAG = "tcp_server";

static struct {
    uint8_t attempts;        // Consecutive failed attempts
    uint32_t lock_until_sec; // Uptime timestamp when lock expires
} s_auth_state = {0};



// This is the main bridge function you were looking for ;)
static void handle_session(int sock)
{
    uint8_t usb_buf[USB_BUF_SIZE];

    const char banner[] = "\r\n--- Remote console open ---\r\n"
                          "(Telnet EOF or disconnect to close session)\r\n\r\n";
    send(sock, banner, strlen(banner), 0);

    ESP_LOGI(TAG, "Bridge session started");

    //----------------------------------------------------------
    // Main IO loop
    // Notice that Telnet → USB is 1 character at a time.
    // Far from optimal. But simpler and enough for a serial console.
    //----------------------------------------------------------
    while (1) {
        fd_set readset;
        FD_ZERO(&readset);
        FD_SET(sock, &readset);
        struct timeval tv = { .tv_sec = 0, .tv_usec = 10000 };

        int activity = select(sock + 1, &readset, NULL, NULL, &tv);
        if (activity < 0) {
            ESP_LOGE(TAG, "Select failed");
            break;
        }

        // --- 1. Telnet → USB
        if (FD_ISSET(sock, &readset)) {
            uint8_t c;
            if (recv(sock, &c, 1, 0) <= 0) {
                ESP_LOGI(TAG, "Client disconnected (TCP close)");
                break;
            }
            
            // IAC handling: supress garbage
            if (c == TELNET_IAC) {
                uint8_t cmd;
                if (recv(sock, &cmd, 1, 0) <= 0) continue;

                // Handle special Telnet commands
                if (cmd == TELNET_IAC) {
                    // Escaped 0xFF → forward as data
                    c = TELNET_IAC;
                }
                else if (cmd == TELNET_EOF) {
                    // Graceful shutdown request
                    ESP_LOGI(TAG, "Received Telnet EOF — closing session");
                    send(sock, "\r\n[Session closed by EOF]\r\n", 31, 0);
                    break;
                }
                else if (cmd == TELNET_SB) {
                    // Skip subnegotiation
                    uint8_t prev = 0, b;
                    while (recv(sock, &b, 1, 0) > 0) {
                        if (prev == TELNET_IAC && b == TELNET_SE) break;
                        prev = b;
                    }
                    continue;
                }
                else if (cmd == TELNET_WILL || cmd == TELNET_WONT ||
                         cmd == TELNET_DO   || cmd == TELNET_DONT) {
                    uint8_t opt;
                    recv(sock, &opt, 1, 0);
                    continue;
                }
                else {
                    // Other IAC: discard (e.g., NOP, BRK, AYT)
                    continue;
                }
            // CR LF handling: supress CR to prevent double "enter"
            } else if (c == TELNET_CR) {
                continue;
            }

            // Forward data to USB
            if (usb_write(&c, 1) != 1) {
                const char error[] = "\r\n--- Remote end not listening ---\r\n";
                send(sock, error, strlen(error), 0);
            }
        }

        // --- 2. USB → Telnet ---
        int total = 0;
        while (1) {
            int rx = usb_read(usb_buf, USB_BUF_SIZE);
            if (rx <= 0) break;

            send(sock, usb_buf, rx, 0);
            total += rx;

            // Safety: avoid infinite loop if USB floods
            if (total > 2048) {
                ESP_LOGW(TAG, "USB flood: >2KB read in one poll");
                break;
            }
        }
    }
    //----------------------------------------------------------
    //----------------------------------------------------------

    ESP_LOGI(TAG, "Bridge session ended");
}


static void handle_telnet_command(int sock)
{
    uint8_t cmd;
    if (recv(sock, &cmd, 1, 0) <= 0) return;

    if (cmd == TELNET_IAC) {
        // Escaped 0xFF — treat as data
        return;
    }

    if (cmd == TELNET_SB) {
        // Skip subnegotiation until IAC SE
        uint8_t prev = 0, b;
        while (recv(sock, &b, 1, 0) > 0) {
            if (prev == TELNET_IAC && b == TELNET_SE) break;
            prev = b;
        }
        return;
    }

    if (cmd == TELNET_WILL || cmd == TELNET_WONT ||
        cmd == TELNET_DO   || cmd == TELNET_DONT) {
        uint8_t option;
        if (recv(sock, &option, 1, 0) <= 0) return;

        // Only respond to ECHO and SGA to enable server-controlled echo
        if (option == TELNET_ECHO || option == TELNET_SGA) {
            uint8_t resp[3] = {
                TELNET_IAC,
                (cmd == TELNET_DO || cmd == TELNET_DONT) ? TELNET_WILL : TELNET_DO,
                option
            };
            send(sock, resp, 3, 0);
        }
        // Ignore other options (no response = WONT/DONT)
    }
}


static bool authenticate_client(int sock)
{
    const uint32_t now = esp_timer_get_time() / 1000000ULL;
    const uint32_t uptime_now = now; // alias for clarity

    // --- Check if currently locked ---
    if (s_auth_state.attempts >= MAX_ATTEMPTS) {
        if (now < s_auth_state.lock_until_sec) {
            uint32_t remaining = s_auth_state.lock_until_sec - now;
            ESP_LOGW(TAG, "Auth: LOCKED - %d attempts, retry in %d sec", 
                     s_auth_state.attempts, remaining);
            char msg[64];
            snprintf(msg, sizeof(msg), "\r\nToo many attempts. Try again in %ld seconds.\r\n", remaining);
            send(sock, msg, strlen(msg), 0);
            return false;
        } else {
            // Lock expired — reset
            ESP_LOGI(TAG, "Auth: Lock expired (%d sec passed). Resetting attempt counter.", 
                     LOCK_TIME_SEC);
            s_auth_state.attempts = 0;
        }
    }

    ESP_LOGI(TAG, "Auth: Prompting for password (attempts so far: %d)", s_auth_state.attempts);
    const char prompt[] = "Password: ";
    send(sock, prompt, strlen(prompt), 0);

    char password[64] = {0};
    size_t idx = 0;
    const char asterisk = '*';

    while (1) {
        uint8_t c;
        struct timeval timeout = { .tv_sec = 30, .tv_usec = 0 };
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

        int ret = recv(sock, &c, 1, 0);
        if (ret <= 0) {
            s_auth_state.attempts++;
            if (s_auth_state.attempts >= MAX_ATTEMPTS) {
                s_auth_state.lock_until_sec = uptime_now + LOCK_TIME_SEC;
                ESP_LOGW(TAG, "Auth: TIMEOUT counted as failed attempt #%d → LOCKING for %d sec",
                         s_auth_state.attempts, LOCK_TIME_SEC);
            } else {
                ESP_LOGW(TAG, "Auth: TIMEOUT - failed attempt #%d", s_auth_state.attempts);
            }
            return false;
        }

        if (c == TELNET_IAC) {
            handle_telnet_command(sock);
            continue;
        }

        if (c == '\r' || c == '\n') {
            send(sock, "\r\n", 2, 0);
            break;
        }

        if (c == 0x7F || c == 0x08) {
            if (idx > 0) {
                idx--;
                send(sock, "\b \b", 3, 0);
            }
            continue;
        }

        if (!isprint(c)) continue;

        if (idx < sizeof(password) - 1) {
            password[idx++] = (char)c;
            send(sock, &asterisk, 1, 0);
        }
    }

    password[idx] = '\0';
    bool success = (strcmp(password, g_config.sespass) == 0);

    if (success) {
        s_auth_state.attempts = 0;
        ESP_LOGI(TAG, "Auth: SUCCESS - password accepted, counter reset");
        return true;
    } else {
        s_auth_state.attempts++;
        if (s_auth_state.attempts >= MAX_ATTEMPTS) {
            s_auth_state.lock_until_sec = uptime_now + LOCK_TIME_SEC;
            ESP_LOGW(TAG, "Auth: FAILED attempt #%d → LOCKING for %d seconds", 
                     s_auth_state.attempts, LOCK_TIME_SEC);
        } else {
            ESP_LOGW(TAG, "Auth: FAILED attempt #%d (max %d)", 
                     s_auth_state.attempts, MAX_ATTEMPTS);
        }
        send(sock, "\r\nAccess denied.\r\n", 19, 0);
        return false;
    }
}


static void handle_client_secure(int sock)
{
    // Request server-controlled echo (raw mode)
    uint8_t negotiation[] = {
        TELNET_IAC, TELNET_WILL, TELNET_ECHO,
        TELNET_IAC, TELNET_WILL, TELNET_SGA
    };
    send(sock, negotiation, sizeof(negotiation), 0);

    const char banner[] = "\r\nRemote Console Telnet Server\r\n";
    send(sock, banner, strlen(banner), 0);

    if (g_config.sespass[0] == '\0' || authenticate_client(sock)) {
        handle_session(sock);
    }
}

// -------------------------------
// Original server task (updated)
// -------------------------------
static void tcp_server_task(void *pvParameters)
{
    char addr_str[128];
    int addr_family = (int)pvParameters;
    int ip_protocol = 0;
    int keepAlive = 1;
    int nodelay = NODELAY_FLAG;
    int keepIdle = KEEPALIVE_IDLE;
    int keepInterval = KEEPALIVE_INTERVAL;
    int keepCount = KEEPALIVE_COUNT;
    struct sockaddr_storage dest_addr;

    if (addr_family == AF_INET) {
        struct sockaddr_in *dest_addr_ip4 = (struct sockaddr_in *)&dest_addr;
        dest_addr_ip4->sin_addr.s_addr = htonl(INADDR_ANY);
        dest_addr_ip4->sin_family = AF_INET;
        dest_addr_ip4->sin_port = htons(g_config.port);
        ip_protocol = IPPROTO_IP;
    }

    int listen_sock = socket(addr_family, SOCK_STREAM, ip_protocol);
    if (listen_sock < 0) {
        ESP_LOGE(TAG, "Unable to create socket: errno %d", errno);
        vTaskDelete(NULL);
        return;
    }
    int opt = 1;
    setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    ESP_LOGI(TAG, "Socket created");

    int err = bind(listen_sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
    if (err != 0) {
        ESP_LOGE(TAG, "Socket unable to bind: errno %d", errno);
        goto CLEAN_UP;
    }
    ESP_LOGI(TAG, "Socket bound, port %d", g_config.port);

    err = listen(listen_sock, 1);
    if (err != 0) {
        ESP_LOGE(TAG, "Error occurred during listen: errno %d", errno);
        goto CLEAN_UP;
    }

    while (1) {
        ESP_LOGI(TAG, "Socket listening");

        struct sockaddr_storage source_addr;
        socklen_t addr_len = sizeof(source_addr);
        int sock = accept(listen_sock, (struct sockaddr *)&source_addr, &addr_len);
        if (sock < 0) {
            ESP_LOGE(TAG, "Unable to accept connection: errno %d", errno);
            break;
        }

        // Apply keep-alive and nodelay
        setsockopt(sock, SOL_SOCKET, SO_KEEPALIVE, &keepAlive, sizeof(int));
        setsockopt(sock, IPPROTO_TCP, TCP_KEEPIDLE, &keepIdle, sizeof(int));
        setsockopt(sock, IPPROTO_TCP, TCP_KEEPINTVL, &keepInterval, sizeof(int));
        setsockopt(sock, IPPROTO_TCP, TCP_KEEPCNT, &keepCount, sizeof(int));
        setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(int));

        // Convert IP to string
        if (source_addr.ss_family == PF_INET) {
            inet_ntoa_r(((struct sockaddr_in *)&source_addr)->sin_addr, addr_str, sizeof(addr_str) - 1);
        }
        ESP_LOGI(TAG, "Socket accepted ip address: %s", addr_str);

        handle_client_secure(sock);

        shutdown(sock, SHUT_RDWR);
        close(sock);
    }

CLEAN_UP:
    close(listen_sock);
    vTaskDelete(NULL);
}

// -------------------------------
// Public start function
// -------------------------------
void tcp_server_start(void)
{
    ESP_LOGI(TAG, "Starting Telnet server...");
    ESP_ERROR_CHECK(esp_netif_init());

    // Pass AF_INET as parameter (your original pattern)
    xTaskCreate(tcp_server_task, "tcp_server", 4096, (void*)AF_INET, 5, NULL);
}