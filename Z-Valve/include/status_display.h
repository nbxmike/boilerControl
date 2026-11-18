#pragma once

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "valve_control.h"

namespace status_display {

constexpr unsigned kLineCount = 4;
constexpr unsigned kLineBufferSize = 20;

/*
 * Map a valve state to the short label used on the display.
 * Args: status - latest valve state.
 * Returns: A static abbreviated status label.
 */
inline const char *motorLabel(valve::Status status) {
    switch (status) {
    case valve::Status::Open: return "Opn";
    case valve::Status::Closed: return "Clsd";
    case valve::Status::Error: return "ERR";
    case valve::Status::Opening:
    case valve::Status::Closing: return "Mov";
    default: return "Unk";
    }
}

/*
 * Format the IP, MQTT, and four per-valve statuses into four 128-pixel display rows.
 * Args: wifiUp/mqttUp - connection state; ipv4 - address octets; motorStatuses - four valve states;
 * lines - receives the four null-terminated display rows.
 * Returns: None.
 */
inline void formatLines(bool wifiUp, bool mqttUp, const uint8_t ipv4[4],
                        const valve::Status *motorStatuses,
                        char (&lines)[kLineCount][kLineBufferSize]) {
    if (wifiUp) {
        snprintf(lines[0], kLineBufferSize, "%u.%u.%u.%u",
                 static_cast<unsigned>(ipv4[0]), static_cast<unsigned>(ipv4[1]),
                 static_cast<unsigned>(ipv4[2]), static_cast<unsigned>(ipv4[3]));
    } else {
        strcpy(lines[0], "Down");
    }
    snprintf(lines[1], kLineBufferSize, "MQTT %s", wifiUp && mqttUp ? "Up" : "Down");
    snprintf(lines[2], kLineBufferSize, "M1:%s M2:%s",
             motorLabel(motorStatuses[0]), motorLabel(motorStatuses[1]));
    snprintf(lines[3], kLineBufferSize, "M3:%s M4:%s",
             motorLabel(motorStatuses[2]), motorLabel(motorStatuses[3]));
}

/*
 * Initialize the SH1106 and refresh it when network or motor state changes and once a minute.
 * Args: p1, p2, p3 - unused Zephyr thread-entry parameters.
 * Returns: None; the display thread runs until initialization fails or the system stops.
 */
void thread(void *, void *, void *);

}
