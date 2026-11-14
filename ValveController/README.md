# Four-Valve ESP32 Controller

PlatformIO/Arduino firmware for an ESP32 DevKitC with an ESP32-WROOM-32 module
and four BTS7960/IBT-2 full-bridge boards, or four FRC-style PWM motor controllers
selected at compile time. The structure follows the neighboring
RelayBoard project. Use a classic dual-core ESP32, not an ESP32-C3/S3 or a
WROVER/PSRAM variant that reserves GPIO16/17.

## Pin Selection and Wiring

In the default BTS7960 configuration, each motor needs two independently routed LEDC PWM signals, one enable signal
driving both bridge enables, and two active-low limit-switch inputs: 12 outputs
and 8 inputs overall. All eight PWM signals use hardware LEDC channels 0-7 at
20 kHz and 8-bit resolution. ESP32's GPIO matrix allows LEDC on every chosen
output; no analog output or software PWM is used. Enables are ordinary GPIO.
Each valve is independent; only R_EN and L_EN **on the same board** are tied.

GPIO34/35/36/39 are input-only and have no internal pull-ups, so they are assigned
to switches. Flash GPIO6-11, UART0 GPIO1/3, and boot-sensitive GPIO0/2/5/12 are
left alone. GPIO15 is used only as a switch input. Low at reset suppresses ROM
UART boot messages and changes unused SDIO-slave timing; it does not prevent
normal flash boot or subsequent firmware serial output. JTAG pins are reused.

The output triples are neighboring header positions, grouped by valve. Valves
1/2 use the left header's central/lower GPIO group; valves 3/4 use the right
header's upper/central GPIO group. Switch pairs use the remaining nearby pins.
Follow **GPIO labels**, not physical header pin numbers or cable colors.

| Valve | RPWM | LPWM | R_EN + L_EN | Fully open switch | Fully closed switch |
| --- | --- | --- | --- | --- | --- |
| 1 | GPIO32 | GPIO33 | GPIO25 | GPIO36 (VP) | GPIO39 (VN) |
| 2 | GPIO26 | GPIO27 | GPIO14 | GPIO34 | GPIO35 |
| 3 | GPIO23 | GPIO22 | GPIO21 | GPIO13 | GPIO15 |
| 4 | GPIO19 | GPIO18 | GPIO17 | GPIO16 | GPIO4 |

Configuration lives in [include/valve_config.h](include/valve_config.h). Startup
checks reject duplicate pins and unsupported GPIO assignments before enabling
outputs. Do not change the board variant or map without rechecking its pinout.

### BTS7960 Motor Modules

- Module header: 1=RPWM, 2=LPWM, 3=R_EN, 4=L_EN, 5=R_IS, 6=L_IS, 7=VCC, 8=GND.
- Use one GPIO per enable pair, **not** a permanently connected 5 V enable.
- Feed module VCC from regulated 5 V and join ESP32, module logic, and motor-supply
  grounds. Connect the motor to M+/M- and a suitable fused motor supply to B+/B-.
  Do not power motors from the DevKitC or connect motor supply to a GPIO.
- R_IS/L_IS are unused and must remain disconnected from the ESP32. Their current
  sense/alarm function is not monitored by this firmware.
- The supplied module manual advertises 3.3-5 V control, but the supplied diagrams
  show varying 74HC244/74AHC244 buffers. Their thresholds at 5 V are not all
  guaranteed compatible with ESP32 3.3 V. For a variant-independent installation,
  route all 12 control outputs through two 74AHCT244 buffers powered at 5 V.
  Tie their active-low output enables low, decouple each with 100 nF, and keep
  unused buffer inputs grounded. Use a noninverting, TTL-input 5 V buffer, not
  a bare 5 V 74HC244 or slow bidirectional I2C level-shifter board.
- Add 10 kohm pull-downs on all ESP32-to-buffer control inputs and on each
  module's enable pair. Confirm the enable pairs stay low when the ESP32 resets,
  is unpowered, or is being flashed. Never assume software protects reset time.
- Provide local supply decoupling, suitable fusing, wire gauge, cooling, and
  motor-supply transient suppression. Avoid backfeeding USB through an external
  DevKitC 5 V supply; follow the board's power-selection instructions.

### FRC PWM Motor Controllers

Select the make/model once with `MOTOR_CONTROLLER` in the configuration header's
default block, or the equivalent build flag. The ready-made `esp32dev-frc-pwm`
PlatformIO environment selects `MOTOR_CONTROLLER_VICTOR_SPX`. The existing
`esp32dev` environment and VS Code tasks remain BTS7960 by default.
**All four motor controllers must be the same make and model.** There is no
per-valve model selection or mixed-driver support. The single selection chooses
the interface, GPIO map, pulse bounds, and preferred frame timing for all four.

FRC mode uses one noninverted, servo-style RC PWM output per motor, hardware
LEDC channels 0-3 at the model's preferred frame rate, with 16-bit resolution. Pulse
width, not percentage duty, commands motor direction/speed. Continuous neutral
pulses start during setup and remain active while stopped, at limits, on timeout,
and during reversal pauses. No separate GPIO enable is used. Pins unused by the
selected map are not configured by the firmware.

| Valve | PWM signal | Fully open switch | Fully closed switch |
| --- | --- | --- | --- |
| 1 | GPIO32 | GPIO36 (VP) | GPIO39 (VN) |
| 2 | GPIO26 | GPIO34 | GPIO35 |
| 3 | GPIO23 | GPIO13 | GPIO14 |
| 4 | GPIO19 | GPIO16 | GPIO4 |

**Rewire valve 3's closed-limit switch from GPIO15 to GPIO14 in FRC mode.**
This map avoids all ESP32 strapping pins. Edit the FRC branch of `kValves` to
remap signals or change each valve's `openDirection`; startup still validates
output capability and uniqueness across all twelve pins.

Set `MOTOR_CONTROLLER` in the header or FRC environment's `build_flags`:

| Controller selection | Controller | Reverse / neutral / forward (us) | Nominal frame (us) | LEDC rate |
| --- | --- | --- | --- | --- |
| `MOTOR_CONTROLLER_VICTOR_SPX` | Victor SPX (FRC default) | 997 / 1500 / 2004 | 5050 | 198 Hz |
| `MOTOR_CONTROLLER_TALON_SRX` | Talon SRX | 997 / 1500 / 2004 | 5050 | 198 Hz |
| `MOTOR_CONTROLLER_MDL_BDC24` | TI MDL-BDC24 / Jaguar | 697 / 1507 / 2310 | 5050 | 198 Hz |
| `MOTOR_CONTROLLER_VICTOR_888` | Victor 888 | 1026 / 1507 / 2027 | 10100 | 99 Hz |
| `MOTOR_CONTROLLER_VICTOR_884` | Victor 884; verify calibration | 1026 / 1507 / 2027 | 10100 | 99 Hz |

`MOTOR_CONTROLLER_BTS7960` selects the original 20 kHz, 8-bit bridge interface
instead of RC PWM. Do not separately define `MOTOR_DRIVER` or `FRC_PWM_MODEL`;
the old two-flag selection is rejected to prevent conflicting configuration.

These are WPILib pulse-width presets, not guarantees of a particular controller's
stored calibration. The Victor 884 shares the 888 preset but may need calibration.
All valves use the selected model/timing. Optional defines override
`FRC_PWM_REVERSE_US`, `FRC_PWM_NEUTRAL_US`, and `FRC_PWM_FORWARD_US` independently.
`FRC_PWM_SPEED_PERMILLE` defaults to 1000 (full speed); 500 halves the pulse-width
excursion from neutral in both directions. This is not a closed-loop speed
setting; small excursions can remain inside the controller's neutral deadband.
`kMotorDuty` applies only to BTS7960 mode.

Frame defaults follow the saved WPILib controller presets: 5.05 ms for CTRE
and Jaguar, 10.10 ms for legacy Victor. LEDC's API accepts integer Hz, so the
nearest configured rates are 198 Hz (approximately 5050.505 us) and 99 Hz
(approximately 10101.010 us). Pulse conversion uses that configured frequency,
not the nominal frame period, avoiding a pulse-width scaling error. Hardware
clock/divider quantization can introduce an additional small timing error;
measure the outputs during commissioning.

`FRC_PWM_PERIOD_US` optionally overrides the selected model's nominal period
for all four controllers. Periods must be 5-30 ms; frequency is rounded to the
nearest integer Hz. For example, 20000 restores the original 50 Hz / 20 ms
baseline, 10000 selects 100 Hz, and 5000 selects 200 Hz. Check the controller's
accepted frame rate before overriding. Pulse bounds, speed, controller, and
period are checked at compile time. The 100 ms reversal pause and travel
timeout remain unchanged and apply to every controller preset.

- Connect each signal to its controller's **PWM input**, not CAN, a limit input,
  or a motor terminal. Configure the controller for PWM operation as required
  by its manual. No CAN communication or controller feedback is implemented.
- Connect the PWM signal return to ESP32 ground according to the controller's
  wiring instructions. Do not connect a servo connector's positive supply lead
  to an ESP32 GPIO or backfeed the DevKitC. Power each motor controller from
  its specified, fused motor supply, not the ESP32.
- Check the exact controller's PWM input voltage requirements. ESP32 outputs
  are 3.3 V; if 5 V signaling is required/supported, use a noninverting
  3.3-to-5 V TTL-input buffer such as 74AHCT125/244. Never feed 5 V back into a
  GPIO. Add a 10 kohm pull-down at each ESP32 PWM output to prevent a floating
  input during reset, flashing, or loss of ESP32 power.
- With the mechanism disconnected, calibrate neutral and endpoints per the
  controller's manual using the selected pulses. Verify neutral produces no
  motion, both directions reach the intended output, and reset/signal loss
  stops the motor. Configure brake/coast behavior on the controller itself.
- Neutral is a stop command, **not electrical isolation or a hardware disable**.
  Allow up to a frame plus controller response time after a stop request.
  Independently test signal-loss behavior; loss of valid pulses during reset
  relies on the controller's own timeout. Use hardwired emergency isolation.

Saved timing references and provenance are in [References/README.md](References/README.md).

### Limit Switches

Use dry contacts or compatible open-collector signals. Each input needs its own
external 4.7-10 kohm pull-up to **3.3 V**, with the switch between GPIO and GND.
A contact closes only at its corresponding end stop. Never apply 5/12/24 V to an
ESP32 input. All switch pins use INPUT mode and rely on these external pull-ups.
Route sensor wiring away from motor wiring; use appropriate filtering/protection
for long cables. A disconnected wire reads inactive and cannot be distinguished
from an unreached stop, so the timeout is only a fallback, not fault detection.

## Motion and Status

`open`/`on` requests opening; `close`/`off` requests closing. Commands are exact,
lowercase payloads without trailing whitespace. Invalid payloads/topics do nothing.

| Open switch low | Closed switch low | Motion | Report |
| --- | --- | --- | --- |
| Yes | Yes | Any; force stopped | `error` |
| Yes | No | Any | `open` |
| No | Yes | Any | `closed` |
| No | No | Opening | `opening` |
| No | No | Closing | `closing` |
| No | No | Stopped or reversing pause | `unknown` |

Neither switch low is a valid mid-travel state and permits starting. An active
destination switch prevents starting. During travel, that destination switch
immediately cancels motion; both switches low also immediately cancels motion.
An active departure switch permits moving away from its stop. Switch states
take precedence over motion when reporting. No automatic restart follows a
limit hit, contradictory switches, or timeout; a fresh command is required.

For BTS7960, clockwise means RPWM active / LPWM zero; counterclockwise means the reverse.
For FRC PWM, clockwise selects pulses above neutral; counterclockwise selects
pulses below neutral.
Opening defaults to clockwise on each valve, closing to counterclockwise.
Actual shaft direction depends on motor wiring. Set a valve's `openDirection`
to `Direction::Counterclockwise` to reverse its opening direction without
changing MQTT command names or switch assignments.

In BTS7960 mode, stopped means both PWM duties zero and both enable pins low (coast/high
impedance, not active braking). A reversal disables the bridge for at least
`kReversalDelayMs` (default 100 ms) before driving the other direction. FRC PWM
instead commands neutral throughout that pause; brake/coast is controller-specific.
Repeating
the same command does not reset the travel timer. `kMaxTravelMs` defaults to
120000 ms: expiry stops the motor and, with neither switch active, reports
`unknown`. Set this to a measured, appropriate maximum before installation.
`kMotorDuty` defaults to 255/255; reduce it if the motor/application requires PWM.

A high-priority FreeRTOS task checks all switches once per RTOS tick, independent
of MQTT connection attempts and reporting. Latest commands arrive through a
separate one-element queue per valve. Wi-Fi/MQTT outages do not pause monitoring:
an existing movement continues to its limit or timeout. Boot never restores
movement locally. Switch handling stops on the first active sample without
software debounce or automatic resumption. Allow for task latency and mechanical
coast distance; use independent hardwired interlocks/emergency isolation where
overtravel can cause damage. This firmware is not a safety-rated controller.

## MQTT

[include/mqtt_config.h](include/mqtt_config.h) defines `MQTT_ROOT_TOPIC` and
`MQTT_CLIENT_ID`, both initially `heater-valves`. It imports the existing local
credential header. Credentials are unchanged and excluded from version control.
Required macros are `WIFI_SSID`, `WIFI_PASSWORD`, `MQTT_BROKER_HOST`,
`MQTT_BROKER_PORT`, and `MQTT_AUTH_ENABLED`; when authentication is enabled,
`MQTT_USERNAME` and `MQTT_PASSWORD` are also required. This matches RelayBoard's
plain TCP MQTT transport; TLS is not configured.

| Topic | Direction | Payload |
| --- | --- | --- |
| `heater-valves/valve1` through `heater-valves/valve4` | Command | `open`, `on`, `close`, `off` |
| `heater-valves/valve1/set` through `heater-valves/valve4/set` | Command alias, matching RelayBoard layout | Same |
| `heater-valves/valve1/state` through `heater-valves/valve4/state` | Retained status | Table above |
| `heater-valves/availability` | Retained availability / last will | `online`, `offline` |

Every 60 seconds one retained status is published in round-robin order:
valve1, valve2, valve3, valve4, then repeat. Each valve therefore has a four-minute
periodic refresh. Changes and reconnect snapshots are published additionally.
Failed periodic publications retry without advancing the valve index. The
60-second wait is a timer, **not sleep/deep sleep**: networking and end-stop
monitoring remain active. Set topics are subscribed at QoS 1; status publishes
use PubSubClient's QoS 0. `offline` is the broker's last will, subject to keepalive
and broker detection delay, not an instantaneous hardware-health guarantee.

Send commands **without MQTT retain**. PubSubClient's callback does not expose
the retain flag, so retained commands will replay on reconnect and may move a
valve. Clear any previously retained commands before commissioning.

## Build and Development

Open [ValveController.code-workspace](ValveController.code-workspace) in VS Code
and install the recommended PlatformIO extension. The project pins Espressif32
8.0.0 (Arduino-ESP32 3.3.x), using the Arduino 3.x LEDC API. Dependencies are in
[platformio.ini](platformio.ini). No secrets are stored in the workspace file.

The Windows workspace includes Build, Upload, Serial Monitor, and Host Tests
tasks. PlatformIO tasks use its standard per-user installation. Host Tests need
`g++` on PATH (for example MinGW); no ESP32 or broker is needed for those tests.
Run the following commands from the project directory in a PlatformIO terminal.
If `pio` is not on PATH in an ordinary PowerShell terminal, replace it with
`& "$env:USERPROFILE/.platformio/penv/Scripts/platformio.exe"`.

### Build Each Motor Controller

For **BTS7960 / IBT-2**, use the default environment without editing any flags:

```powershell
pio run -e esp32dev
```

Plain `pio run` and the existing VS Code Build task also select this environment.

For **FRC PWM**, open [platformio.ini](platformio.ini) and set the single
`-DMOTOR_CONTROLLER=...` line under `[env:esp32dev-frc-pwm]` to the desired value.
This selects the same controller for all four motors:

| Motor controller | Model build flag |
| --- | --- |
| Victor SPX (default) | `-DMOTOR_CONTROLLER=MOTOR_CONTROLLER_VICTOR_SPX` |
| Talon SRX | `-DMOTOR_CONTROLLER=MOTOR_CONTROLLER_TALON_SRX` |
| TI MDL-BDC24 / Jaguar | `-DMOTOR_CONTROLLER=MOTOR_CONTROLLER_MDL_BDC24` |
| Victor 888 | `-DMOTOR_CONTROLLER=MOTOR_CONTROLLER_VICTOR_888` |
| Victor 884 | `-DMOTOR_CONTROLLER=MOTOR_CONTROLLER_VICTOR_884` |

Replace the controller line rather than adding a second definition. No separate
driver flag is needed. This build flag overrides the header's default controller.
Then build any of the five FRC selections with:

```powershell
pio run -e esp32dev-frc-pwm
```

To verify both drivers with the currently selected FRC model:

```powershell
pio run -e esp32dev -e esp32dev-frc-pwm
```

Build commands do not upload. To deliberately flash the corresponding firmware,
run **only the upload command matching the connected hardware**:

```powershell
pio run -e esp32dev --target upload
pio run -e esp32dev-frc-pwm --target upload
```

The existing VS Code Upload task selects BTS7960, not FRC PWM. Before changing
drivers, disconnect motor power and follow the selected pin map and calibration
instructions above, including the FRC GPIO15-to-GPIO14 limit-switch change.

To monitor the connected controller:

```powershell
pio device monitor --baud 115200
```

Set `upload_port`/`monitor_port` in the environment if automatic serial-port
selection is ambiguous. Uploading is manual; no upload runs when opening the
workspace.

### Prepare and Refresh Dependencies

Use an approved network that can reach the PlatformIO Registry and package
download hosts. No separate FRC motor library is needed; PWM uses the ESP32
framework's built-in LEDC API. Reference documents are not build dependencies.

To install missing libraries, frameworks, and toolchains for **both** environments
before working offline, then verify the builds:

```powershell
pio pkg install -e esp32dev -e esp32dev-frc-pwm
pio run -e esp32dev -e esp32dev-frc-pwm
```

`pio run` also installs missing dependencies for the environments it builds,
but it does not generally refresh already-installed packages to newer versions.
Plain `pio run` prepares only the default BTS7960 environment.

To check for available updates without installing them:

```powershell
pio pkg outdated -e esp32dev -e esp32dev-frc-pwm
```

To deliberately refresh dependencies within the configured version constraints,
then verify the resulting builds:

```powershell
pio pkg update -e esp32dev -e esp32dev-frc-pwm
pio run -e esp32dev -e esp32dev-frc-pwm
```

In [platformio.ini](platformio.ini), `espressif32@8.0.0` pins the platform to
that exact release; adopting a newer platform requires an explicit configuration
change and retesting. `PubSubClient@^2.8` allows compatible 2.x library updates,
not an unrestricted upgrade to any version. Refreshing dependencies does not
necessarily install the latest upstream Arduino framework or ESP32 SDK.

Retain the project's `.pio/libdeps` directory and the installed platforms/tools
under `%USERPROFILE%\.platformio` for subsequent offline builds. Deleting these
or selecting new packages, versions, or tools can require fresh downloads. If a
download fails, retry the install on an approved network with access; do not
disable certificate verification. A successful build of both environments is
the practical readiness check, though it does not guarantee every optional
debugging or analysis tool is available offline.

### Host Tests

The host test task compiles and runs
[test/test_valve_control.cpp](test/test_valve_control.cpp), covering all limit
combinations, command synonyms, invalid commands, direction reversal dead time,
timeout, time-counter rollover, GPIO uniqueness, and valve independence.
It also tests the selected motor-output mapping. To test all FRC presets as
well as the default driver on the host:

```powershell
foreach ($controller in 1..6) {
  g++ -std=c++11 -Wall -Wextra -Werror -I include "-DMOTOR_CONTROLLER=$controller" test/test_valve_control.cpp -o "$env:TEMP/valve-control-tests.exe"
  if ($LASTEXITCODE -ne 0) { throw 'Host test compile failed' }
  & "$env:TEMP/valve-control-tests.exe"
  if ($LASTEXITCODE -ne 0) { throw 'Host tests failed' }
}
```

## Commissioning

1. With motor power disconnected, verify every GPIO and pull-up/pull-down,
   inactive boot outputs, and module control levels using a meter/logic analyzer.
    For FRC PWM, measure the selected frame period (approximately 5.05 ms for
    CTRE/Jaguar or 10.10 ms for legacy Victor by default) and the selected
    neutral/forward/reverse pulse widths on all four outputs, including during
    a reversal pause. Verify all four installed controllers match the selection.
2. Verify each switch alone produces `open` or `closed`; both grounded produces
   `error`; neither produces `unknown`. A contradictory pair must not start.
3. Use a current-limited motor supply and decouple the valve if practical. Check
   each motor's physical direction and correct `openDirection` before travel.
4. Test both destination stops, reversals, all four motors together, network
   outages during motion, reset, and travel timeout. Confirm overrun is acceptable.
5. Observe one-valve-per-minute refreshes and retained changes with an MQTT
   client. Do not rely on the broker alone to verify actual motor disable.

Reference material: the supplied BTS7960 module manual (header pin functions),
Infineon BTS7960 datasheet (IN/INH behavior), module circuit diagrams (input
buffers), and Espressif DevKitC/ESP32 documents (GPIO capabilities and straps).
Compilation and host tests do not replace these powered hardware checks.