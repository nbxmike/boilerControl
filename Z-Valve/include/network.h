#pragma once

#include <stdint.h>

namespace network {

struct DisplaySnapshot {
    bool wifiUp = false;
    bool mqttUp = false;
    uint8_t ipv4[4]{};
};

/*
 * Return the last observed network state and a copied IPv4 address for display.
 * Returns: Wi-Fi/MQTT readiness and the preferred IPv4 address, if available.
 */
DisplaySnapshot displaySnapshot();

}

/*
 * Run Wi-Fi and MQTT connection management, command reception, and status reporting.
 * Args: None.
 * Returns: None; the network manager runs indefinitely.
 */
void runNetwork();
