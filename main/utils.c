#include "utils.h"
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <stdio.h>

bool is_valid_ipv4(const char *ip) {
    if (!ip) return false;
    unsigned char a, b, c, d;
    return (sscanf(ip, "%hhu.%hhu.%hhu.%hhu", &a, &b, &c, &d) == 4);
}

bool is_valid_port(const char *port_str, uint16_t *out_port) {
    if (!port_str) return false;
    char *end;
    unsigned long val = strtoul(port_str, &end, 10);
    if (*end != '\0' || val == 0 || val > 65535) return false;
    if (out_port) *out_port = (uint16_t)val;
    return true;
}

bool is_valid_ssid(const char *ssid) {
    if (!ssid) return false;
    size_t len = strlen(ssid);
    return (len > 0 && len <= 32);
}

bool is_valid_password(const char *pass) {
    if (!pass) return false;
    size_t len = strlen(pass);
    return (len <= 64); // can be empty? In your case, no — but validation allows empty; logic will reject it in `config_is_valid`
}