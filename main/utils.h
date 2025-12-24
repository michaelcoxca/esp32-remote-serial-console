#ifndef UTILS_H
#define UTILS_H

#include <stdint.h>
#include <stdbool.h>

bool is_valid_ipv4(const char *ip);
bool is_valid_port(const char *port_str, uint16_t *out_port);
bool is_valid_ssid(const char *ssid);
bool is_valid_password(const char *pass);

#endif