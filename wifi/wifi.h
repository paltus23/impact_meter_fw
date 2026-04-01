#ifndef WIFI_H
#define WIFI_H

#include <stdbool.h>

/**
 * Initialise WiFi.
 * Attempts STA connection to saved credentials first; if no credentials are
 * stored or connection fails within 5 s, falls back to AP mode (SSID:
 * "impact_meter", open network, 192.168.4.1).
 * Must be called after settings_init().
 */
void wifi_init(void);

/**
 * Returns true when the device is connected to a WiFi station and has an IP.
 */
bool wifi_is_sta_connected(void);

#endif /* WIFI_H */
