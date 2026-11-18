#pragma once

#include "valve_control.h"

namespace valve {

/*
 * Match a valve command topic without assuming the incoming topic is NUL-terminated.
 * Args:
 * topic - topic bytes.
 * length - topic size in bytes.
 * root - configured topic prefix.
 * Returns: Zero-based valve index, or -1 when the topic is not a valve command.
 */
inline int commandValve(const uint8_t *topic, size_t length, const char *root) {
    const size_t prefix = strlen(root);
    if (length != prefix + 7 && length != prefix + 11) return -1;
    if (memcmp(topic, root, prefix) != 0 ||
        memcmp(topic + prefix, "/valve", 6) != 0 ||
        topic[prefix + 6] < '1' || topic[prefix + 6] > '4') return -1;
    if (length == prefix + 11 && memcmp(topic + prefix + 7, "/set", 4) != 0) return -1;
    return topic[prefix + 6] - '1';
}

}
