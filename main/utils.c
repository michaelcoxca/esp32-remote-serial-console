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

bool is_valid_ssid(const char *ssid) {
    if (!ssid) return false;
    size_t len = strlen(ssid);
    return (len > 0 && len <= 32);
}

bool is_valid_password(const char *pass) {
    if (!pass) return false;
    return (strlen(pass) <= 64);
}