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
#include "ringbuf.h"

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

#define TCP_BUF_SIZE     CONFIG_LWIP_TCP_MSS  // TCP I/O chunk size (<= Maximum Segment Size)

#define KEEPALIVE_IDLE     5    // Start keepalive after X sec idle
#define KEEPALIVE_INTERVAL 1    // Probe every X sec
#define KEEPALIVE_COUNT    5    // Fail after X probes
#define NODELAY_FLAG       1    // Disable Nagle

#define MAX_ATTEMPTS 3
#define LOCK_TIME_SEC 60  // Lockout duration (seconds)

static const char *TAG = "tcp_server";

static ringbuf_t usb_read_rb = NULL;

static struct {
    uint8_t attempts;
    uint32_t lock_until_sec;
} s_auth_state = {0};

// ========================
// Telnet Utility Helpers
// ========================

// Reads one byte with optional timeout (in seconds).
// Returns -1 on error/timeout, 0-255 on success.
static int telnet_recv_byte(int sock, int timeout_sec) {
    if (timeout_sec >= 0) {
        struct timeval tv = { .tv_sec = timeout_sec, .tv_usec = 0 };
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    }

    uint8_t c;
    int ret = recv(sock, &c, 1, 0);
    if (ret <= 0) return -1;
    //ESP_LOGI(TAG, "Sock received byte: %02x", c);
    return (int)c;
}

// Processes a Telnet byte. Returns:
//   -2 --> session should terminate (e.g., EOF)
//   -1 --> skip (handled internally)
// 0-255 --> data byte to forward
static int process_telnet_byte(int sock, uint8_t c) {
    if (c != TELNET_IAC) {
        return (int)c;
    }

    int cmd = telnet_recv_byte(sock, 5);
    if (cmd < 0) return -1;

    if (cmd == TELNET_IAC) {
        return TELNET_IAC; // Escaped 0xFF
    }

    if (cmd == TELNET_EOF) {
        ESP_LOGI(TAG, "Received Telnet EOF -- closing session");
        const char msg[] = "\r\n[Session closed by EOF]\r\n";
        send(sock, msg, strlen(msg), 0);
        return -2;
    }

    if (cmd == TELNET_SB) {
        // Skip subnegotiation until IAC SE
        int prev = 0;
        int b;
        while ((b = telnet_recv_byte(sock, 5)) >= 0) {
            if (prev == TELNET_IAC && b == TELNET_SE) break;
            prev = b;
        }
        return -1;
    }

    if (cmd == TELNET_WILL || cmd == TELNET_WONT ||
        cmd == TELNET_DO   || cmd == TELNET_DONT) {
        int opt = telnet_recv_byte(sock, 5);
        if (opt < 0) return -1;

        // Only respond to ECHO and SGA
        if (opt == TELNET_ECHO || opt == TELNET_SGA) {
            uint8_t resp[3] = {
                TELNET_IAC,
                (cmd == TELNET_DO || cmd == TELNET_DONT) ? TELNET_WILL : TELNET_DO,
                (uint8_t)opt
            };
            send(sock, resp, 3, 0);
        }
        return -1;
    }

    // Ignore other IAC commands (NOP, BRK, AYT, etc.)
    return -1;
}

// ========================
// Authentication
// ========================

static bool authenticate_client(int sock) {
    const uint32_t now = esp_timer_get_time() / 1000000ULL;

    // Check lockout
    if (s_auth_state.attempts >= MAX_ATTEMPTS) {
        if (now < s_auth_state.lock_until_sec) {
            uint32_t remaining = s_auth_state.lock_until_sec - now;
            ESP_LOGW(TAG, "Auth: LOCKED - retry in %d sec", remaining);
            char msg[64];
            snprintf(msg, sizeof(msg), "\r\nToo many attempts. Try again in %ld seconds.\r\n", (long)remaining);
            send(sock, msg, strlen(msg), 0);
            return false;
        }
        // Lock expired: reset
        ESP_LOGI(TAG, "Auth: Lock expired. Resetting counter.");
        s_auth_state.attempts = 0;
    }

    ESP_LOGI(TAG, "Auth: Prompting (attempts: %d)", s_auth_state.attempts);
    send(sock, "Password: ", 10, 0);

    char password[64] = {0};
    size_t idx = 0;
    const char asterisk = '*';

    while (1) {
        int c = telnet_recv_byte(sock, 30);
        if (c < 0) {
            ESP_LOGW(TAG, "Auth: TIMEOUT");
            goto auth_fail;
        }

        int result = process_telnet_byte(sock, (uint8_t)c);
        if (result == -2) {
            // EOF during auth: treat as disconnect
            return false;
        }
        if (result < 0) {
            continue;
        }

        c = result;

        if (c == '\r' || c == '\n') {
            send(sock, "\r\n", 2, 0);
            break;
        }

        if (c == 0x7F || c == 0x08) { // Backspace / DEL
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
        ESP_LOGI(TAG, "Auth: SUCCESS");
        return true;
    }

auth_fail:
    s_auth_state.attempts++;
    if (s_auth_state.attempts >= MAX_ATTEMPTS) {
        s_auth_state.lock_until_sec = now + LOCK_TIME_SEC;
        ESP_LOGW(TAG, "Auth: FAILED #%d -> LOCKING", s_auth_state.attempts);
    } else {
        ESP_LOGW(TAG, "Auth: FAILED #%d", s_auth_state.attempts);
    }
    send(sock, "\r\nAccess denied.\r\n", 19, 0);
    return false;
}

// ========================
// Main Session Loop
// ========================

static void handle_session(int sock) {
    const char banner[] = "\r\n--- Remote console open ---\r\n"
                          "(Telnet EOF or disconnect to close session)\r\n\r\n";
    send(sock, banner, strlen(banner), 0);
    ESP_LOGI(TAG, "Bridge session started");

    const TickType_t MAX_TIME_BETWEEN_PAUSE = pdMS_TO_TICKS(50); // Yield every this ms
    TickType_t last_pause_time = xTaskGetTickCount();

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

        // Telnet -> USB (minimal latency)
        if (FD_ISSET(sock, &readset)) {
            int c = telnet_recv_byte(sock, 0);
            if (c < 0) {
                ESP_LOGI(TAG, "Client disconnected (TCP close)");
                break;
            }

            int result = process_telnet_byte(sock, (uint8_t)c);
            if (result == -2) break; // EOF
            if (result < 0) continue;

            c = result;

            // --- Line-ending normalization ---
            static bool prev_was_cr = false;
            uint8_t out_byte = 0;
            bool emit = false;

            if (prev_was_cr) {
                prev_was_cr = false;
                if (c == TELNET_LF || c == 0) {
                    // CR LF or CR NUL -> LF
                    out_byte = TELNET_LF;
                    emit = true;
                } else {
                    // CR followed by something else: treat CR as LF, and reprocess current char
                    // Emit LF first
                    out_byte = TELNET_LF;
                    emit = true;
                    // And now reprocess 'c' in next iteration
                    if (usb_write(&out_byte, 1) != 1) {
                        const char err[] = "\r\n--- Remote end not listening ---\r\n";
                        send(sock, err, strlen(err), 0);
                    }
                    // Now treat 'c' as a fresh input byte
                    // (fall through to normal processing below)
                }
            }

            if (!emit) {
                if (c == TELNET_CR) {
                    prev_was_cr = true;
                    continue; // don't emit yet
                } else if (c == TELNET_LF) {
                    out_byte = TELNET_LF;
                    emit = true;
                } else if (c == 0) {
                    out_byte = TELNET_LF;
                    emit = true;
                } else {
                    out_byte = (uint8_t)c;
                    emit = true;
                }
            }

            if (emit) {
                if (usb_write(&out_byte, 1) != 1) {
                    const char err[] = "\r\n--- Remote end not listening ---\r\n";
                    send(sock, err, strlen(err), 0);
                }
            }
        }

        // USB -> Telnet (from ring buffer)
        if (ringbuf_count(usb_read_rb) == ringbuf_capacity(usb_read_rb) - 1) {
            const char err[] = "--- USB buffer was full - some data may have been dropped ---\r\n";
            send(sock, err, strlen(err), 0);
        }
        uint8_t tcp_buf[TCP_BUF_SIZE];
        int rx = ringbuf_get_bytes(usb_read_rb, tcp_buf, TCP_BUF_SIZE);
        send(sock, tcp_buf, rx, 0);

        // Pause if needed
        TickType_t now = xTaskGetTickCount();
        TickType_t time_elapsed = now - last_pause_time;

        if (time_elapsed >= MAX_TIME_BETWEEN_PAUSE) {
            vTaskDelay(1); // minimal yield for watchdog
            last_pause_time = xTaskGetTickCount();
        }

    }

    ESP_LOGI(TAG, "Bridge session ended");
}

// ========================
// Client Entry Point
// ========================

static void handle_client_secure(int sock) {
    // Request server-controlled echo and suppress go-ahead
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

    shutdown(sock, SHUT_RDWR);
    close(sock);
}

// ========================
// Server Task
// ========================
void tcp_server_task(void *pvParameters) {
    ringbuf_t rb = (ringbuf_t)pvParameters;
    
    if (!rb) {
        ESP_LOGE(TAG, "Ring buffer is NULL!");
        vTaskDelete(NULL);
        return;
    }
    
    usb_read_rb = rb;
    
    char addr_str[128];
    int addr_family = AF_INET;
    int ip_protocol = 0;
    int keepAlive = 1;
    int nodelay = NODELAY_FLAG;
    int keepIdle = KEEPALIVE_IDLE;
    int keepInterval = KEEPALIVE_INTERVAL;
    int keepCount = KEEPALIVE_COUNT;
    struct sockaddr_storage dest_addr;

    ESP_LOGI(TAG, "Starting Telnet server...");

    ESP_ERROR_CHECK(esp_netif_init());

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
        ESP_LOGE(TAG, "Error during listen: errno %d", errno);
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

        // Apply socket options
        setsockopt(sock, SOL_SOCKET, SO_KEEPALIVE, &keepAlive, sizeof(int));
        setsockopt(sock, IPPROTO_TCP, TCP_KEEPIDLE, &keepIdle, sizeof(int));
        setsockopt(sock, IPPROTO_TCP, TCP_KEEPINTVL, &keepInterval, sizeof(int));
        setsockopt(sock, IPPROTO_TCP, TCP_KEEPCNT, &keepCount, sizeof(int));
        setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(int));

        // Log client IP
        if (source_addr.ss_family == PF_INET) {
            inet_ntoa_r(((struct sockaddr_in *)&source_addr)->sin_addr, addr_str, sizeof(addr_str) - 1);
        }
        ESP_LOGI(TAG, "Socket accepted ip address: %s", addr_str);

        handle_client_secure(sock);
    }

CLEAN_UP:
    close(listen_sock);
    vTaskDelete(NULL);
}
