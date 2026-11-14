#pragma once

// Credentials stay in the local, Git-ignored header; do not put private values here.
#include "config_secrets.h"

// Broker client identity and topic namespace. Use a unique client ID for each deployed ESP32.
// Keep the root topic aligned with the application's command and state subscriptions.
#define MQTT_CLIENT_ID "heater-valves"
#define MQTT_ROOT_TOPIC "heater-valves"