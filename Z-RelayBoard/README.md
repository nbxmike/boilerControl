# Thermostat Router Firmware

Firmware for the ESP32-WROOM-32 / ESP32 DevKitC V4 installed on the ES32A08 board with the processor target being the classic ESP32. The ES32A08 74HC595 output pins and integrated display use the pin assignments from its board support package.

## MQTT

Wi-Fi, MQTT, and Omada credentials are kept in the ignored `include/config_secrets.h`. The broker host and port can be changed there to values reachable on your network. This file is local and is not included in a clone; securely transfer it to each build machine. The firmware connects to 2.4 GHz Wi-Fi, as supported by the ESP32.

For each zone, publish one of `bypass`, `on`, or `off` to:

| Zone | Command topic | Relay pair |
| --- | --- | --- |
| 1 | `thermostat-router/zone1/set` | 1 (odd), 2 (even) |
| 2 | `thermostat-router/zone2/set` | 3 (odd), 4 (even) |
| 3 | `thermostat-router/zone3/set` | 5 (odd), 6 (even) |
| 4 | `thermostat-router/zone4/set` | 7 (odd), 8 (even) |

The retained state is published to the corresponding `.../state` topic. A received valid command updates its zone's state immediately. In addition, the firmware publishes one zone every 120 seconds in round-robin order, completing a full pass every eight minutes. Broker availability is retained at `thermostat-router/availability`.

Relay states follow the requested mapping: `bypass` deenergizes both contacts, `on` energizes both, and `off` energizes only the even-numbered relay. All zones start in `bypass` after reset. The display shows `b`, `1`, or `0` for each zone from left to right, and shows `----` for five seconds every 25 seconds.
