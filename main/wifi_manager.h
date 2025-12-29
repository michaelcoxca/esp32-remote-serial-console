// wifi_manager.h
#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <stdbool.h>

// Start Wi-Fi connection in a dedicated task
void wifi_manager_start(void);

// Stop Wi-Fi
void wifi_manager_stop(void);

// Returns true if connected
bool wifi_manager_is_connected(void);

#endif // WIFI_MANAGER_H