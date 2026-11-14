# ESP32_Relay_X8 Schematic Overview, Pinout & Zephyr RTOS + LAN8651 Integration Guide

![Product](FrontBack.jpg "Product Image")

This board is an **ESP32-WROOM-32E 8-Channel Wi-Fi/BLE Relay Module** (commonly known as the **ESP32_Relay_X8**). Though I have not found schematics for this generic module, the general architecture, GPIO mapping, and pinouts are documented using sources available on the web such as the ESPHome description pages.  Expansion plans for an integration guide for running **Zephyr RTOS** with a **Microchip LAN8651 10BASE-T1S MAC-PHY** for experiments are included.

---

## Part 1: Hardware Schematic Overview & Pinout

### 1. Relay-to-GPIO Schematic Mapping
Each of the eight **Songle SRD-05VDC-SL-C** relays is driven by a dedicated ESP32 GPIO pin using an non-isolated circuit which works as **active-HIGH** logic.

| Relay Channel | ESP32 GPIO | Logic Trigger | Terminal Connections |
| :--- | :--- | :--- | :--- |
| **Relay 1** | **GPIO 32** | Active HIGH | NC1, COM1, NO1 |
| **Relay 2** | **GPIO 33** | Active HIGH | NC2, COM2, NO2 |
| **Relay 3** | **GPIO 25** | Active HIGH | NC3, COM3, NO3 |
| **Relay 4** | **GPIO 26** | Active HIGH | NC4, COM4, NO4 |
| **Relay 5** | **GPIO 27** | Active HIGH | NC5, COM5, NO5 |
| **Relay 6** | **GPIO 14** | Active HIGH | NC6, COM6, NO6 |
| **Relay 7** | **GPIO 12** | Active HIGH | NC7, COM7, NO7 |
| **Relay 8** | **GPIO 13** | Active HIGH | NC8, COM8, NO8 |

* **Driver Stage:** Between each ESP32 GPIO and the 5V relay coil is an **NPN switching transistor**, a **current-limiting base resistor**, a **flyback diode** (across the coil to absorb back-EMF), and an **SMD status LED**.
* **Relay Terminals:** Each relay breaks out three screw terminals along the top edge: **NC** (Normally Closed), **COM** (Common), and **NO** (Normally Open), rated up to **10A at 250V AC or 30V DC**.
* **Unlabelled** The relay connectors are not discretely numbered, though it is clear from the physical layout which grouping is associated with which relay.

---

### 2. Power Supply Circuit
The power section on the bottom right uses a two-stage voltage regulation design:
* **Input Terminal (3-pin green block):** Accepts either a **7–30V DC** input (via the `7-30V` and `GND` pins) or a regulated **5V DC** input (via the `5V` and `GND` pins).  A dual output supply feeding both the 7-30V and 5V **must not be used.**
* **5V Buck Converter:** An onboard switching regulator (typically an **LM2596S-5.0** with the `330` 33µH inductor) steps the 7–30V DC input down to **5V DC** to power the relay coils.
* **3.3V LDO Regulator:** An **AMS1117-3.3** linear regulator steps the 5V rail down to **3.3V DC** to power the ESP32-WROOM-32E module.

---

### 3. Onboard Controls, LEDs & Headers
* **Programming Header (6-pin row above the ESP32):** Breaks out **GND, RX, TX, 5V, GPIO0, and 3.3V** (exact order is printed on the underside of the PCB). Because there is no onboard USB-to-UART chip, you must use an external **3.3V USB-to-TTL adapter** to flash firmware.
* **Push Buttons:**
  * **EN:** Hardware reset button (pulls the ESP32 `EN` pin to GND).
  * **IO0:** Boot mode button (pulls **GPIO 0** to GND when pressed; hold while pressing `EN` to enter flashing mode).
* **Onboard LEDs:** 
  * One red power indicator LED, 
  * Eight individual relay status LEDs, and 
  * One user-programmable LED connected to **GPIO 23**.
* **Expansion Headers:** The two **2×10 pin headers** flanking the ESP32 break out all remaining GPIOs, power rails (3.3V, 5V), and GND pins (labeled on the back of the board).

> **Safety Warning:** When switching mains AC voltage (120V/240V) through the relay screw terminals, keep high-voltage wiring isolated from the low-voltage ESP32 side and never touch the back of the PCB while powered.

---

## Zephyr RTOS & 10BASE-T1S Feasibility


The onboard **ESP32-WROOM-32E** module is supported by Zephyr (as `esp32_devkitc_wroom`), and Zephyr includes native mainline driver support for the MircoChip LAN8650/1 (`microchip,lan865x`) and Analog Devices ADIN1140 (`eth_adin1140.c`) starting in **Zephyr v4.1.0+**.

### 1. Hardware & Memory Sufficiency
* **CPU & Silicon Revision:** The ESP32-WROOM-32E uses the **ESP32-D0WD-V3** (Rev 3) dual-core Xtensa LX6 chip at 240 MHz. Zephyr requires ESP32 silicon Rev 3 or newer, so this module meets the baseline requirement.
* **Flash & SRAM:** The module includes **4 MB of SPI Flash** and **520 KB of SRAM**. A standard Zephyr networking application with the TC6 SPI driver, IPv4/IPv6 stack, and relay GPIO control uses roughly 300–500 KB of Flash and 60–120 KB of SRAM, leaving substantial headroom.
* **3.3V Power Rail:** The LAN8651 operates at **3.3V** and draws ~45–60 mA during active 10BASE-T1S transmission. The onboard **AMS1117-3.3** LDO regulator on this relay board can easily supply the extra current alongside the ESP32.

---

### 2. GPIO & SPI Bus Availability
The **LAN8651** communicates via **OPEN Alliance TC6 SPI** and requires **6 signal pins**: `MOSI`, `MISO`, `SCK`, `CS` (Chip Select), `IRQ` (Interrupt, active-low), and `RST` (Reset, active-low).

Even with all 8 relays (`GPIO 12, 13, 14, 25, 26, 27, 32, 33`), the status LED (`GPIO 23`), and UART0 (`GPIO 1, 3`) occupied, **the board still has 11 completely free GPIOs** broken out on the two 2×10 expansion headers.

#### Recommended LAN8651 Pin Mapping (Using VSPI / `spi3`)
Because the default HSPI pins (`GPIO 12, 13, 14`) are used by Relays 6–8, map the LAN8651 to the **VSPI (`spi3`)** controller using these unobstructed pins:

| LAN8651 Pin | ESP32 GPIO | Pin Capabilities & Notes |
| :--- | :--- | :--- |
| **SCK** | **GPIO 18** | Native VSPI Clock (`spi3`) |
| **MISO** | **GPIO 19** | Native VSPI MISO (`spi3`) |
| **MOSI** | **GPIO 21** | Free bidirectional GPIO (routed via GPIO matrix; GPIO 23 is used by onboard LED) |
| **CS (SS)** | **GPIO 5** | Native VSPI CS (`spi3`), strapping pin (pulled high at boot, safe for SPI CS) |
| **IRQ (INT)** | **GPIO 34** | Input-only pin with no internal pull-up; **add an external 10 kΩ pull-up to 3.3V** (or use **GPIO 22**) |
| **RST** | **GPIO 16** | Free bidirectional GPIO (avoid GPIO 15 if you want clean boot logs, or use **GPIO 17**) |

*(Note: **GPIO 4**, **GPIO 22**, **GPIO 35**, **GPIO 36**, and **GPIO 39** also remain completely free for sensors or status inputs.)*

---

### 3. Zephyr Integration Caveats
* **Custom Devicetree Overlay:** Because this board is not a standard DevKitC, build against `-b esp32_devkitc_wroom/esp32/procpu` and provide a `.overlay` file defining `spi3`, the `microchip,lan8651` child node, and the `gpio-leds` / `zephyr,user` relay bindings.
* **SPI Clock Speed:** The LAN8651 supports up to **25 MHz** SPI clock. Using the ESP32 GPIO matrix (since `MOSI` is moved from GPIO 23 to GPIO 21) supports up to **26.6 MHz**, so running the LAN8651 at **15–20 MHz** is reliable over jumper wires.
* **Flashing:** Use an external **3.3V USB-to-UART adapter** connected to the 6-pin programming header (`TX`, `RX`, `GND`, `3.3V`) to run `west flash`.
