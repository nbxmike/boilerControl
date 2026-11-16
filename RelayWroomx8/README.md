# Thermostat Router Firmware

Firmware for the **ESP32-WROOM-32E 8-Channel Wi-Fi/BLE Relay Module** (aka **ESP32_Relay_X8**). This is a low cost IoT development board built around Espressif’s dual-core ESP32-WROOM-32E MCU.  Firmware is based on the Zephyr OS.

## Relay Module Purpose and Connections

### Processor

The ESP32-WROOM-32E is a low cost Wi-Fi, Bluetooth, and BLE wireless microcontroller module manufactured by Espressif Systems.  Major specification of this module are:
- Dual-core Xtensa 32-bit LX6 microprocessor running at up to 240 MHz (ESP32-D0WD-V3).
- 448 KB ROM
- 520 KB on-chip SRAM
- 16 KB RTC SRAM
- Integrated 4 MB SPI flash (may be altered based on actual delivery)
- On-board PCB trace antenna

### Relay Wire Routing

| Pin\Port | Relay1   | Relay3   | Relay5   | Relay7   |
|----------|-------|-------|-------|-------|
| 1-NO        | 24VAC | 24VAC | 24VAC | 24VAC |
| 2-COM       | Relay2,1 | Relay4,1 | Relay6,1 | Relay8,1 |
| 3-NC       | -     | -     | -     | -     |

| Pin\Port | Relay2   | Relay4   | Relay6   | Relay8   |
|----------|-------|-------|-------|-------|
| 1-NO        | Relay1,2 | Relay3,2 | Relay5,2 | Relay7,2 |
| 2-COM       | FZ1   | FZ2   | FZ3   | FZ4   |
| 3-NC       | TZ1   | TZ2   | TZ3   | TZ4   |

Where:

- FZx is the furnace's control input for each zone; e.g. FZ1 is furnace zone 1 control normally connected to a physical thermostat.
- TZx is the thermostat output for each zone; e.g. TZ3 is the thermostat for zone 3's legacy wiring.

## Connections / Wiring

GPIO numbers below are ESP32 signal numbers, not programming-header positions. All GPIO signals use **3.3 V logic**; never connect 24 VAC or 5 V directly to a GPIO.

### Relay GPIOs and Contact Logic

All eight relay drivers are **active-high**:

| GPIO output | Coil | COM to NO (terminal 2 to 1) | COM to NC (terminal 2 to 3) |
| --- | --- | --- | --- |
| LOW / `0` | Deenergized | Open | Closed |
| HIGH / `1` | Energized | Closed | Open |

Thus, "relay off" does not mean all contacts are open: the normally closed path conducts when the coil is off.

| Relay | Zone | ESP32 GPIO | Role in the wiring above |
| --- | --- | --- | --- |
| 1 | 1 | GPIO32 | Odd relay: switches 24 VAC to relay 2's NO input |
| 2 | 1 | GPIO33 | Even relay: selects thermostat TZ1 (LOW/NC) or relay 1 (HIGH/NO) for FZ1 |
| 3 | 2 | GPIO25 | Odd relay: switches 24 VAC to relay 4's NO input |
| 4 | 2 | GPIO26 | Even relay: selects thermostat TZ2 (LOW/NC) or relay 3 (HIGH/NO) for FZ2 |
| 5 | 3 | GPIO27 | Odd relay: switches 24 VAC to relay 6's NO input |
| 6 | 3 | GPIO14 | Even relay: selects thermostat TZ3 (LOW/NC) or relay 5 (HIGH/NO) for FZ3 |
| 7 | 4 | GPIO12 | Odd relay: switches 24 VAC to relay 8's NO input |
| 8 | 4 | GPIO13 | Even relay: selects thermostat TZ4 (LOW/NC) or relay 7 (HIGH/NO) for FZ4 |

The resulting routing for each zone is:

| Mode | Odd relay GPIO | Even relay GPIO | Furnace zone connection |
| --- | --- | --- | --- |
| `bypass` | LOW | LOW | Connected to its physical thermostat through the even relay's NC contact |
| `on` | HIGH | HIGH | Connected to 24 VAC through both relays' NO contacts; thermostat disconnected |
| `off` | LOW | HIGH | Connected to the odd relay's open NO path; thermostat and forced 24 VAC disconnected |

The odd relays' NC terminals are unused. The firmware initializes every relay LOW at startup; GPIO/contact states before initialization are not guaranteed.

### Reserved SPI / 10BASE-T1S Connections

These expansion-header signals are reserved for a future SPI MAC/PHY. SPI3 is currently disabled; no Ethernet hardware is required.

| Signal | ESP32 GPIO | Direction at ESP32 | Function |
| --- | --- | --- | --- |
| SPI3 SCK | GPIO18 | Output | SPI clock |
| SPI3 MISO | GPIO19 | Input | Data from MAC/PHY |
| SPI3 MOSI | GPIO21 | Output | Data to MAC/PHY |
| SPI3 CS | GPIO5 | Output | Active-low chip select: LOW selects, HIGH deselects |
| IRQ / INT | GPIO34 | Input only | Active-low interrupt; requires an external 10 kohm pull-up to 3.3 V |
| MAC/PHY reset | GPIO16 | Output | Active-low reset: LOW asserts reset, HIGH releases |

Reservation does not mean the disabled bus actively drives these levels. GPIO5 is also a boot-strapping pin; attached hardware must preserve the board's required reset-time levels.

### LED GPIO and Logic

| Indicator | ESP32 GPIO | LOW / `0` | HIGH / `1` |
| --- | --- | --- | --- |
| Programmable onboard status LED | GPIO23 | LED OFF | LED ON |
| Individual relay indicators | Same GPIO/driver as the corresponding relay above | Indicator OFF / coil deenergized | Indicator ON / coil energized |
| Power indicator | None; power-driven | Not GPIO-controlled | Not GPIO-controlled |

GPIO23 displays Wi-Fi/MQTT connection status using the patterns in **Onboard LED** below. Relay indicators are not independently controlled.

### Other Onboard / Reserved Signals

| Signal | GPIO / pin | Use and wiring restriction |
| --- | --- | --- |
| IO0 / BOOT button | GPIO0 | Pressing pulls LOW; hold LOW while resetting to enter the serial bootloader. Keep available for programming. |
| EN / RESET button | EN (not a GPIO) | Pressing pulls EN LOW to reset/disable the ESP32; release to run. |
| UART0 TX | GPIO1 | Board transmit / boot and firmware logs; connect to a 3.3 V USB-UART adapter's RX. |
| UART0 RX | GPIO3 | Board receive / flashing; connect to the adapter's TX. |
| Internal SPI flash | GPIO6 through GPIO11 | Used inside the WROOM module; do not use for expansion wiring. |
| Boot-strapping signals | GPIO0, GPIO2, GPIO5, GPIO12, GPIO15 | Sampled during reset; external devices must not force incompatible levels. GPIO12 is already relay 7: do not pull it HIGH during reset. |

Use the PCB's printed programming-header labels to identify pin positions and connect a common GND to the USB-UART adapter. The board has no onboard USB-UART bridge. Its 5 V and 3.3 V power pins are not GPIOs; do not treat them as interchangeable. GPIO34 through GPIO39, where exposed, are input-only and have no internal pull-up/down resistors.

## MQTT

Wi-Fi and MQTT credentials are kept in the ignored `include/config_secrets.h`. Copy `include/sample_cfg_secrets.include` to that path and edit the values locally. Set `MQTT_AUTH_ENABLED` to `0` for a broker without authentication. An empty `WIFI_PASSWORD` selects an open network; otherwise WPA2-PSK is used. The broker host may be an IPv4 address or DNS name. Never commit this header or share build outputs containing credentials.

For each zone, publish one of `bypass`, `on`, or `off` to:

| Zone | Command topic | Relay pair |
| --- | --- | --- |
| 1 | `thermostat-router/zone1/set` | 1 (odd), 2 (even) |
| 2 | `thermostat-router/zone2/set` | 3 (odd), 4 (even) |
| 3 | `thermostat-router/zone3/set` | 5 (odd), 6 (even) |
| 4 | `thermostat-router/zone4/set` | 7 (odd), 8 (even) |

The retained state is published to the corresponding `.../state` topic. A received valid command updates its zone's state immediately. In addition, the firmware publishes one zone every 120 seconds in round-robin order, completing a full pass every eight minutes. Broker availability is retained at `thermostat-router/availability`.

Relay states follow the requested mapping: `bypass` deenergizes both contacts, `on` energizes both, and `off` energizes only the even-numbered relay. All zones start in `bypass` after reset. 

Commands are exact lowercase payloads without whitespace. Command subscriptions accept QoS 0 or 1; state and availability publications are retained QoS 0. Availability is `online` after subscriptions and initial state publications succeed, with a retained `offline` last will on connection loss. Client IDs include the board's Wi-Fi MAC address.

MQTT uses unencrypted TCP, including broker credentials. Use only a trusted, isolated network; TLS is not configured.

Use **non-retained commands**: a retained command is replayed by the broker on reconnection and can replace reset bypass. All four current states are published on each MQTT connection, including bypass after reset. Wi-Fi and MQTT retry automatically; zones keep their last commanded modes during network outages (there is no network-loss bypass timeout). The periodic reporter sends one overdue zone after an outage rather than a catch-up burst.

Relay transitions include a 20 ms settling interval: the even relay disconnects the thermostat before the odd relay applies 24 VAC, and the odd relay removes 24 VAC before the even relay reconnects the thermostat. GPIO write failures are logged, all relays are deenergized where possible, and the MCU resets; no successful state is published for a failed transition.

## Network
The firmware connects to 2.4 GHz Wi-Fi, as supported by the ESP32 using credentials stored in the file `include/config_secrets.h`.

Pins are reserved for an SPI bus and IRQ input suitable for a 10BASE-T1S SPI MAC/PHY such as the Microchip LAN8651 or similar device from Analog Devices, Texas Instruments or Onsemi.

`app.overlay` reserves SPI3 SCK=GPIO18, MISO=GPIO19, MOSI=GPIO21, CS=GPIO5, IRQ=GPIO34, and reset=GPIO16. SPI3 remains disabled and no Ethernet device is instantiated. GPIO34 has no internal pull-up; add an external 10 kohm pull-up to 3.3 V when connecting an active-low IRQ. SPI2 and the default I2C controller are disabled to avoid the relay and reserved-pin mappings.

## Onboard LED

The Onboard LED shall illuminated in the following patterns:
- When connected to the MQTT broked, it should blinks at 500mS on and 500mS off
- When connected to WiFi but not MQTT, it should blink at 200mS on and 200mS off
- When powered on but WiFi SSID has not been joined blink at 2S on, 500mS off

The GPIO23 active-high LED runs in a separate thread, so broker/DNS/association waits do not stall blinking. The Wi-Fi-only pattern starts at association, including while waiting for DHCP. Pattern changes take effect at the next LED edge.

## Build and Flash

Requires Zephyr 4.5.0 or newer, its Espressif modules/blobs, the ESP32 Xtensa toolchain, CMake, Ninja, Python, and west. The supplied hardware references use an older board name; the checked Zephyr 4.5 target is `esp32_devkitc/esp32/procpu`. The overlay matches the WROOM-32E's 4 MB flash and no PSRAM.

In a configured Zephyr west workspace, from this directory:

```powershell
Copy-Item include\sample_cfg_secrets.include include\config_secrets.h
# Edit include\config_secrets.h before building.
west blobs fetch hal_espressif
west build -b esp32_devkitc/esp32/procpu -d build .
west flash -d build --esp-device COM5
```

Skip the copy if the secrets header already exists. Substitute your adapter's COM port. Use a **3.3 V logic-level USB-UART adapter**, TX to board RX, RX to board TX, and common GND. Power the relay board from its appropriate separate supply, not the adapter's signal pins. Hold IO0, press/release EN, then release IO0 to enter the serial bootloader if automatic flashing is unavailable. UART0 logs use 115200 baud.

GPIO12 (relay 7) is an ESP32 strapping pin: external circuitry must not pull it high during reset. Verify actual board GPIOs, contact wiring, power supply, and all three routing modes on a disconnected low-voltage bench setup before connecting furnace controls. Software initialization cannot guarantee contact states before the MCU starts or after a hardware fault.

## Host Tests

The portable routing logic can be tested without hardware or credentials using a native C compiler:

```powershell
cmake -S tests -B build-tests
cmake --build build-tests
ctest --test-dir build-tests --output-on-failure
```

Tests cover all nine mode transitions in every zone, break-before-make ordering, GPIO failure propagation, strict command/topic parsing, the three LED patterns, and the 120-second round-robin schedule. Hardware flashing, Wi-Fi/MQTT interoperability, and physical contact/LED verification still require a board and reachable broker.

Validation: Host tests passed, and a full linked firmware image was built for `esp32_devkitc/esp32/procpu` using Zephyr 4.5.0 and Zephyr SDK 1.0.1. The build produced `build-sdk\zephyr\zephyr.elf` and `build-sdk\zephyr\zephyr.bin`. Hardware flashing and bench verification still require a connected board and a disconnected low-voltage setup.