# ESP32-WROOM-32E 8-Channel Relay Module (`ESP32_Relay_X8`) Schematic & Pinout Reference

This board is an **ESP32-WROOM-32E 8-Channel Wi-Fi/BLE Relay Module** (commonly known as the **ESP32_Relay_X8**).

While manufacturers rarely publish full CAD schematics for this generic module, its complete circuit architecture, GPIO mapping, and pinouts are well documented across open-source hardware repositories.

---

## 1. Relay-to-GPIO Schematic Mapping

Each of the eight **Songle SRD-05VDC-SL-C** relays is driven by a dedicated ESP32 GPIO pin using **active-HIGH** logic.

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

* **Driver Stage:** Between each ESP32 GPIO and the 5V relay coil is an **NPN switching transistor** (typically S8050/J3Y), a **current-limiting base resistor**, a **flyback diode** (across the coil to absorb back-EMF), and an **SMD status LED**.
* **Relay Terminals:** Each relay breaks out three screw terminals along the top edge: **NC** (Normally Closed), **COM** (Common), and **NO** (Normally Open), rated up to **10A at 250V AC or 30V DC**.

---

## 2. Power Supply Circuit

The power section on the bottom right uses a two-stage voltage regulation design:

* **Input Terminal (3-pin green block):** Accepts either a **7–30V DC** input (via the `7-30V` and `GND` pins) or a regulated **5V DC** input (via the `5V` and `GND` pins).
* **5V Buck Converter:** An onboard switching regulator (typically an **LM2596S-5.0** with the `330` 33µH inductor) steps the 7–30V DC input down to **5V DC** to power the relay coils.
* **3.3V LDO Regulator:** An **AMS1117-3.3** linear regulator steps the 5V rail down to **3.3V DC** to power the ESP32-WROOM-32E module.

---

## 3. Onboard Controls, LEDs & Headers

* **Programming Header (6-pin row above the ESP32):** Breaks out **GND, RX, TX, 5V, GPIO0, and 3.3V** (exact order is printed on the underside of the PCB). Because there is no onboard USB-to-UART chip, you must use an external **3.3V USB-to-TTL adapter** to flash firmware.
* **Push Buttons:**
  * **EN:** Hardware reset button (pulls the ESP32 `EN` pin to GND).
  * **IO0:** Boot mode button (pulls **GPIO 0** to GND when pressed; hold while pressing `EN` to enter flashing mode).
* **Onboard LEDs:** One red power indicator LED, eight individual relay status LEDs, and one user-programmable LED connected to **GPIO 23**.
* **Expansion Headers:** The two **2×10 pin headers** flanking the ESP32 break out all remaining GPIOs, power rails (3.3V, 5V), and GND pins (labeled on the back of the board).

> **Warning:** When switching mains AC voltage (120V/240V) through the relay screw terminals, keep high-voltage wiring isolated from the low-voltage ESP32 side and never touch the back of the PCB while powered.
