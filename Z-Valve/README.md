# Z-Valve: Zephyr ESP32-S3 valve controller

Zephyr 4.5.0 firmware for an **ESP32-S3-DevKitC-1 with an
ESP32-S3-WROOM-1-N16R8 module** (16 MB flash and 8 MB OPI PSRAM). The
application uses the flash but leaves PSRAM disabled. This ports the neighboring
[Arduino ValveController](../ValveController/README.md), without depending on
Arduino, PlatformIO, or files in that project. The pin map is specific to the
ESP32-S3-DevKitC-1 N16R8; do not use it with the original ESP32 DevKitC.

## Preserved behavior

- Four independent valves; BTS7960/IBT-2 bridges by default, or four matching
  FRC-style RC PWM controllers.
- Exact lowercase `open`/`on` and `close`/`off` commands on either command topic.
- Active-low, externally pulled-up open/closed switches. A destination switch
  or contradictory switches immediately cancels active and pending movement.
  An active departure switch still allows moving away from that end stop.
- A minimum 100 ms stopped/neutral reversal interval and a 120,000 ms travel
  timeout. Repeated same-direction commands do not extend a running timer.
  No automatic restart after a limit hit, conflict, timeout, or reboot.
- Physical limits take precedence over motion when reporting position.
- Retained status on change (normally within about 100 ms), on reconnect, and
  one valve every 60 seconds in round-robin order (four minutes per valve).
- Retained `online` availability and an `offline` last will, 30-second MQTT
  keepalive, QoS 1 command subscriptions, and QoS 0 status publications.
- Wi-Fi/DHCP, IPv4 broker DNS lookup, optional MQTT username/password, and
  connection retries spaced at least five seconds apart.

A separate priority-2 Zephyr thread samples switches every 1 ms; networking
runs at priority 7. Sampling is not a hard real-time guarantee: interrupts and
higher-priority kernel/driver work can add latency. Network outages do not stop
an existing movement: it continues to its destination or timeout, just as in the
Arduino version. Atomic per-valve mailboxes hold the latest request.

GPIO/PWM failures are logged and latch the affected valve off until reboot,
with status `error`. Stopping attempts all available stop outputs even after
an I/O failure; failed hardware cannot be guaranteed to stop by software.
Missing/invalid network configuration is logged and disables networking;
outputs remain stopped and the motor supervisor remains running.

## Wiring

Follow **GPIO numbers**, not header positions. The BTS7960 profile supports
four bridges:

| Valve | BTS RPWM | BTS LPWM | BTS R_EN + L_EN | Open limit | Closed limit |
| --- | --- | --- | --- | --- | --- |
| 1 | 4 | 5 | 17 | 39 | 40 |
| 2 | 6 | 7 | 18 | 41 | 42 |
| 3 | 8 | 9 | 19 | 47 (1) | 48 (1) |
| 4 | 15 | 16 | 20 | 21 | 38 (2) |

| Valve | RC PWM signal | Open limit | Closed limit |
| --- | --- | --- | --- |
| 1 | 4 | 39 | 40 |
| 2 | 6 | 41 | 42 |
| 3 | 8 | 47 (1) | 48 (1) |
| 4 | 15 | 21 | 38 (2) |

The same eight limit inputs are used in both controller profiles. Limit
switches are active-low, dry contacts to ground. Use individual external
4.7-10 kohm pull-ups to 3.3 V on GPIO21, 38-42. (1) **GPIO47 and GPIO48 are in
the module's 1.8 V domain**: use suitable 3.3-to-1.8 V level shifting or a
correctly designed divider; never connect a 3.3 V pull-up directly to either
pin. (2) GPIO38 is also connected to the DevKitC's addressable RGB LED; this
firmware does not control that LED. Do not use GPIO0, 3, 45, or 46 for these
signals; they are strapping pins.

The local SH1106 128x64 display uses I2C address `0x3c`, SDA GPIO1, and SCL
GPIO2. Power the module from 3.3 V and use I2C pull-ups to 3.3 V. The display
shows the IPv4 address (or `Down`), MQTT readiness, and two rows of abbreviated
valve states. It refreshes on observed changes and every 60 seconds; each
refresh advances the SH1106 start line by one pixel, wrapping after 32 pixels.
Zephyr's framebuffer driver and character font handle rendering, while the
SH1106 start-line register wraps the shifted frame at the panel edge.

SPI2 is enabled and its pins are reserved for a future peripheral: SCLK GPIO12,
MISO GPIO13, MOSI GPIO11, and CS GPIO10 (idle high). GPIO14 is configured as an
input for the future device's IRQ. There is no SPI target node, target-specific
driver, or IRQ handler yet; IRQ polarity and pull requirements must be checked
when the device is selected. GPIO19/20 are repurposed from native USB D-/D+ as
BTS enable outputs, so use the board's USB-to-UART connector on GPIO43/44 for
flashing and the serial console.

Use the Arduino project's detailed
[motor wiring and electrical precautions](../ValveController/README.md#pin-selection-and-wiring).
In particular:

- Tie the two enable inputs together **only within each BTS module**, controlled
  by its assigned GPIO. Provide external pull-downs on all motor-control
  signals and each BTS enable pair so reset/flashing cannot enable a motor.
- Check module input thresholds; use a suitable noninverting TTL-input
  3.3-to-5 V buffer where needed. A bare 5 V 74HC244 is not guaranteed suitable.
- Use a separate fused motor supply, common signal ground, appropriate wiring,
  decoupling and transient suppression. Never power motors from the DevKitC.
- RC PWM stop is continuous neutral, not electrical isolation. Verify controller
  calibration and signal-loss behavior with the mechanism disconnected.
- Provide independent hardwired interlocks/emergency isolation. A disconnected
  switch wire reads inactive; the travel timeout is not broken-wire detection.
  This is **not a safety-rated controller**.

Wiring is defined in [boards/common.dtsi](boards/common.dtsi),
[boards/bts7960.overlay](boards/bts7960.overlay), and
[boards/frc_pwm.overlay](boards/frc_pwm.overlay). The common overlay configures
the 16 MB flash memory region, while `prj.conf` sets the matching image/flasher
size. It reserves GPIOs used by module flash/PSRAM and leaves the 8 MB PSRAM
disabled. Zephyr's inherited partition map still occupies only the first 4 MB;
the remaining flash is currently unpartitioned. The board's USB-to-UART port
remains available; native USB data does not. The local datasheets are
[SH1106](../References/Display/SH1106.pdf)
and [SSD1306](../References/Display/SSD1306.pdf). Add `open-counterclockwise;`
to a valve node to reverse its opening direction without changing MQTT or
limit semantics.

## Build and configuration

### How to run the command blocks

**Copy and paste the commands into an interactive PowerShell 7 session.**
You do not need to create a `.bat`, `.cmd`, or `.ps1` file.

For setup:

1. Open a standalone **PowerShell 7** window (`pwsh -NoProfile`).
2. Copy the contents of **one command block at a time**. Do not copy the
   Markdown fence lines (` ```powershell ` and ` ``` `), headings, or any
   `PS C:\...>` prompt text.
3. Paste the commands at the PowerShell prompt. Press **Enter** if they have
   not started executing.
4. Wait for that block to finish. Check its expected output and resolve any
   error before pasting the next block.
5. Keep using the **same PowerShell window** so variables such as `$App` and
   `$Python` remain available. A new window does not retain those variables.

When a block asks for input, type the requested answer and press **Enter**;
do not paste the next command block as the answer. The repository-path prompt
below is deliberately separated from the following commands.

### Choose one layout; do not combine their paths

These instructions are for **Windows x86-64 and PowerShell 7**. The application does
not have to be inside a Zephyr workspace. If you already followed Zephyr's
getting-started instructions, keep that environment and use the **external
layout** below. No further external setup guide is required.

| Item | External layout used below | Existing repository-local layout |
| --- | --- | --- |
| Python environment | `%USERPROFILE%\zephyrproject\.venv` | `boilerControl\Z-Valve\.venv` |
| west workspace root | `%USERPROFILE%\zephyrproject` | `boilerControl` |
| Zephyr sources | `%USERPROFILE%\zephyrproject\zephyr` | `boilerControl\zephyr` |
| Downloaded modules | `%USERPROFILE%\zephyrproject\modules` | `boilerControl\modules` |
| Application sources | Your repository's `Z-Valve` directory | Same |
| Build directory | `Z-Valve\build-external` | `Z-Valve\build` |

`%USERPROFILE%` is your Windows home directory. The commands use
`$env:USERPROFILE`, so no username needs to be edited. Your Git repository
can be anywhere else; it does **not** need to be moved into `zephyrproject`.
Do not copy or recreate its Python environment inside the application.

The external layout uses the manifest in `zephyrproject\zephyr\west.yml`.
The repository-local layout uses this application's [west.yml](west.yml).
Both must select **Zephyr 4.5.0** and its matching HAL, mbedTLS, and
TF-PSA-Crypto commits.
**west manages downloaded sources, not Git submodules.**

For a machine with no Zephyr environment yet, start with
[Addendum: complete Windows setup from scratch](#addendum-complete-windows-setup-from-scratch)
at the end of this document, then return here.

### Use an existing external Zephyr workspace

Copy and paste each numbered command block **into the same standalone
PowerShell 7 window**, following the instructions above. No script file is
needed.
Do not use the old repository-local VS Code terminal for this initial
verification. Each block specifies its working directory or uses absolute
paths. Stop at any error; do not continue on the assumption it succeeded.

#### 1. Identify the two independent directories and the exact Python

This block can be run from any directory. At the prompt, enter the actual
full path to your Git repository root (the directory containing `Z-Valve`).
Enter it without surrounding quotes. Do not enter `zephyrproject` unless
that really contains your repository.

```powershell
$ErrorActionPreference = "Stop"
$PSNativeCommandUseErrorActionPreference = $true
$ZephyrWorkspace = Join-Path $env:USERPROFILE "zephyrproject"
$Repo = (Resolve-Path (Read-Host "Full path to your boilerControl repository")).Path
```

When PowerShell asks `Full path to your boilerControl repository`, type your
repository's full path without surrounding quotes and press **Enter**.
Wait until the normal PowerShell prompt returns. **Only then** copy and paste
this next block into the same window:

```powershell
$App = Join-Path $Repo "Z-Valve"
$Python = Join-Path $ZephyrWorkspace ".venv\Scripts\python.exe"
$ZephyrBase = Join-Path $ZephyrWorkspace "zephyr"
$Sdk = Join-Path $env:USERPROFILE "zephyr-sdk-1.0.1"
$Dtc = Join-Path $env:LOCALAPPDATA "Microsoft\WinGet\Packages\oss-winget.dtc_Microsoft.Winget.Source_8wekyb3d8bbwe\usr\bin\dtc.exe"

foreach ($path in @($Python, "$ZephyrWorkspace\.west\config", "$ZephyrBase\west.yml", "$App\CMakeLists.txt")) {
    if (-not (Test-Path $path)) { throw "Required file missing: $path" }
}
Set-Location $ZephyrWorkspace
$env:PATH = "$(Split-Path $Python);$env:PATH"
$env:ZEPHYR_BASE = $ZephyrBase
$env:ZEPHYR_TOOLCHAIN_VARIANT = "zephyr"
& $Python -c "import sys; print(sys.executable); print(sys.prefix)"
& $Python -m west topdir
& $Python -m west config manifest.path
```

Expected: Python and its prefix are under `zephyrproject\.venv`; `west topdir`
is `zephyrproject`; and `manifest.path` is `zephyr` for a standard
getting-started workspace. If `.west\config` or the Python executable is
missing, use the addendum to complete that specific missing setup step;
do not delete or reinitialize an existing workspace.

Run workspace management commands **from `$ZephyrWorkspace`** in this layout.
A `.west` directory in your Git repository can otherwise make west select that
workspace first, even when `ZEPHYR_BASE` points elsewhere. Merely activating
the right Python does not select the right workspace. The build commands
below deliberately use a different working directory to handle Windows
drive-letter differences without selecting the repository-local workspace.

#### 2. Verify Zephyr's version and install this environment's build packages

Do not change directories manually; remain in `$ZephyrWorkspace`.
Paste the following block into the same PowerShell session. Its first command
ensures you are in that directory before running the remaining commands.

```powershell
Set-Location $ZephyrWorkspace
$ZephyrCommit = (& git -C $ZephyrBase rev-parse HEAD).Trim()
if ($ZephyrCommit -ne "41d10b59730406bbab4ca9f1fbbe9e96aafb4966") {
    throw "This application requires Zephyr v4.5.0. Current commit: $ZephyrCommit"
}
& $Python -m pip install "west>=0.14.0" "cmake>=3.20.5" ninja
& $Python -m pip install -r "$ZephyrBase\scripts\requirements-base.txt"
& $Python --version
& $Python -m west --version
cmake --version
ninja --version
```

The commit above is Zephyr's `v4.5.0` tag. If it differs, do **not** silently
upgrade the firmware or reset a workspace used by other projects. Create a
separate v4.5.0 workspace using the addendum, choose a different directory
name, and substitute that path for `$ZephyrWorkspace` in these instructions.
The external editor defaults also need that same directory-name adjustment.

Python must be at least 3.12 and CMake at least 3.20.5. Step 1 puts the external
environment's `Scripts` directory on `PATH`; installing
packages via `& $Python -m pip` cannot accidentally install into the app's
different environment. Existing satisfactory packages are not upgraded by
these commands. Script activation and a `(.venv)` prompt are not required:
the explicit Python executable is the environment selection.

#### 3. Select the matching HAL and crypto modules, then obtain binary libraries

Working directory: `$ZephyrWorkspace`. If you have edited downloaded
dependency repositories, save those changes before updating them; do not
use force/reset commands.

```powershell
Set-Location $ZephyrWorkspace
& $Python -m west update hal_espressif mbedtls tf-psa-crypto
& $Python -m west manifest --validate
& $Python -m west list hal_espressif mbedtls tf-psa-crypto -f "{name}: {path} @ {revision}"
git -C "$ZephyrWorkspace\modules\hal\espressif" rev-parse HEAD
git -C "$ZephyrWorkspace\modules\crypto\mbedtls" rev-parse HEAD
git -C "$ZephyrWorkspace\modules\crypto\tf-psa-crypto" rev-parse HEAD
& $Python -m pip install -r "$ZephyrWorkspace\modules\hal\espressif\zephyr\requirements.txt"
& $Python -m west blobs fetch hal_espressif
& $Python -m west blobs list hal_espressif -f "{status} {path}"
```

Expected HAL commit: `cc81db00233af6020f2fea9fbb0d0098fb3d0d86`.
Expected mbedTLS commit: `c43f34b93797e81fd0257c30004dcd0ae332ae51`.
Expected TF-PSA-Crypto commit: `765af96a20a2b408474dcd950372ba9af4d26b62`.
The manifest check should succeed without output. Every blob status should
be `A` (present with the expected hash), not `D` (missing) or `M` (hash
mismatch). The ESP32 Wi-Fi binary libraries are required.

Only these three module repositories are required by this application. A
standard workspace with additional modules already downloaded is also fine.
Do not run `west init -l .` here or in the application.

#### 4. Verify DTC, 7-Zip, and the exact ESP32-S3 toolchain

Working directory: `$ZephyrWorkspace`. If the DTC path from step 1 is missing,
install the known-working Windows package:

```powershell
winget install --id oss-winget.dtc --exact --source winget --scope user
```

If 7-Zip is not installed, install it:

```powershell
winget install --id 7zip.7zip --exact --source winget
```

Then, in the same setup terminal:

```powershell
if (-not (Test-Path $Dtc)) { throw "DTC missing at $Dtc; select the installed dtc.exe path" }
$env:PATH = "$(Split-Path $Dtc);C:\Program Files\7-Zip;$env:PATH"
& $Dtc --version
Get-Command 7z
```

DTC must run without missing-DLL errors and be at least 1.4.6. The documented
WinGet package supplies DTC 1.6.1. If your DTC or 7-Zip has a different
installation directory, assign `$Dtc` its actual full executable path and
put the actual 7-Zip directory on `PATH`. The build below passes DTC
explicitly, so another broken compiler earlier on the original `PATH`
cannot be selected instead.

The required SDK is **1.0.1**, and the required compiler is
**`xtensa-espressif_esp32s3_zephyr-elf`**, not the classic ESP32 compiler.
Install that toolchain only if its executable is missing:

```powershell
Set-Location $ZephyrWorkspace
$Compiler = Join-Path $Sdk "xtensa-espressif_esp32s3_zephyr-elf\bin\xtensa-espressif_esp32s3_zephyr-elf-gcc.exe"
if (-not (Test-Path $Compiler)) {
    if (Test-Path Env:\ZEPHYR_SDK_INSTALL_DIR) { Remove-Item Env:\ZEPHYR_SDK_INSTALL_DIR }
    & $Python -m west sdk install --version 1.0.1 --install-dir $Sdk --gnu-toolchains xtensa-espressif_esp32s3_zephyr-elf
}
if (-not (Test-Path $Compiler)) { throw "ESP32-S3 compiler still missing: $Compiler" }
$env:ZEPHYR_SDK_INSTALL_DIR = $Sdk
& $Compiler --version
& $Python -m west sdk list
```

The SDK listing must show 1.0.1 at `$Sdk` and this compiler under
**gnu-installed-toolchains**, not merely **gnu-available-toolchains**. If you already
installed SDK 1.0.1 elsewhere, assign `$Sdk` that actual directory before
this block and use it consistently in the editor configuration too.
ESP-IDF is neither required nor a replacement for this SDK.

#### 5. Preserve private configuration and build the application by absolute path

Remain in `$ZephyrWorkspace`, **not** the Git repository. All
application/configuration/build paths below are absolute:

```powershell
if (-not (Test-Path "$App\local.conf")) {
    Copy-Item "$App\local.conf.example" "$App\local.conf"
}
notepad "$App\local.conf"
```

Edit and save the file before the next block. Enter your network/broker
settings and select `CONFIG_APP_MOTOR_CONTROLLER`; existing Arduino users
can transfer their private values, but an Arduino installation is not needed.
The Git-ignored file is merged after [prj.conf](prj.conf). Do not overwrite an
existing `local.conf`. A blank Wi-Fi password selects an open network;
otherwise use a WPA2-PSK passphrase of 8-63 characters. Use a unique MQTT
client ID for each device.

```powershell
Set-Location $ZephyrWorkspace
$Top = (& $Python -m west topdir).Trim()
if ((Resolve-Path $Top).Path -ne (Resolve-Path $ZephyrWorkspace).Path) {
    throw "Wrong west workspace selected for building: $Top"
}
& $Python -m west build -p always -b esp32s3_devkitc/esp32s3/procpu -d "$App\build-external" $App -- "-DDTC=$Dtc"
if ($LASTEXITCODE -ne 0) { throw "Build failed; do not flash an old image" }
Get-Item "$App\build-external\zephyr\zephyr.bin"
Select-String -Path "$App\build-external\CMakeCache.txt" -Pattern "^(ZEPHYR_BASE|WEST_PYTHON|DTC):"
```

Expected: a successful build, a newly generated `zephyr.bin`, and cache paths
pointing to the **external** Zephyr and Python plus the selected DTC. The
new `build-external` directory avoids reusing a cache from the broken mixed
setup. `-p always` rebuilds that directory's configuration; it does not
delete application sources or `local.conf`. Building does not flash hardware.

**Why the directory matters:** west discovers a workspace by searching the
current directory and its parents. Starting in `$ZephyrWorkspace` ensures it
does not select the repository's own `.west`; the explicit `topdir` check
confirms the selected workspace before building. Zephyr 4.5 supports an
application on a different Windows drive from the workspace.

### Reopening and building in VS Code

Open [Z-Valve.code-workspace](Z-Valve.code-workspace) or the `Z-Valve` folder,
not just the repository root. The tracked
[settings](.vscode/settings.json) and [tasks](.vscode/tasks.json) deliberately
offer **two separate choices**:

| Layout | New terminal profile | Build task |
| --- | --- | --- |
| External workspace | `Zephyr External (PowerShell)` | `Zephyr: Build (external workspace)` |
| Repository-local workspace | `Zephyr (PowerShell)` | `Zephyr: Build` |

For the external layout, use **Terminal: Create New Terminal (With Profile)**
in the Command Palette and choose **Zephyr External (PowerShell)**. This
activates `zephyrproject\.venv`, restores the external Zephyr/SDK environment,
sets `$App`, `$Python`, and `$Dtc`, and starts in `zephyrproject`. Run:

```powershell
Set-Location $ZephyrWorkspace
& $Python -m west topdir
& $Python -m west build -b esp32s3_devkitc/esp32s3/procpu -d "$App\build-external" $App -- "-DDTC=$Dtc"
```

Alternatively, use **Tasks: Run Task** and choose
**Zephyr: Build (external workspace)**. That task supplies its own external
Python, working directory, and environment, even if your terminal is wrong.
For Python editor integration, use **Python: Select Interpreter**, choose
**Enter interpreter path**, and select
`%USERPROFILE%\zephyrproject\.venv\Scripts\python.exe`.

**Ctrl+Shift+B still selects the original repository-local task.** Do not
use it for the external layout unless you deliberately change the default
build task. Likewise, the existing CMake Tools configuration is for the
repository-local layout; use the external west task, not CMake Tools Build.
The local defaults remain unchanged so existing machines keep working.

Create a fresh terminal after switching profiles; an old or restored terminal
retains its old environment. If your external workspace is not named
`zephyrproject`, or SDK/DTC/7-Zip are elsewhere, update the **external profile
and external task** paths consistently. Do not replace unrelated local-profile
paths or commit usernames/private machine-specific adjustments.

Outside VS Code, rerun step 1 in a new terminal, restore the tool `PATH` and
`ZEPHYR_SDK_INSTALL_DIR` from step 4, then run the absolute-path build command.
Do not repeat initialization, pip installation, module updates, or SDK
installation just to reopen the project.

### Working on two machines

Commit/push application changes on one machine and pull on the other.
Each machine keeps its own Python environment, SDK, private `local.conf`,
and build directories. Do not copy virtual environments or CMake caches
between machines; they contain host executables or absolute paths.

Application-only changes normally need only another build. If the project's
required Zephyr/dependency versions change, update the chosen workspace
deliberately and rebuild pristine; do not independently `git pull` the HAL
or other downloaded repositories. An external workspace uses its own manifest,
so changing the app's `west.yml` does not automatically change that workspace.

### Controller selection and flashing

Select the motor controller with `CONFIG_APP_MOTOR_CONTROLLER` in `local.conf`.
The supported names and models are listed there. The same selection controls
the Devicetree pin overlay, compile-time motor behavior, and startup log; all
four valves must use that model. When building multiple variants, change this
setting before each build and use a separate pristine build directory. These
commands use the **external layout** and variables defined above:

```powershell
# With CONFIG_APP_MOTOR_CONTROLLER="BTS7960" in local.conf
Set-Location $ZephyrWorkspace
& $Python -m west build -p always -b esp32s3_devkitc/esp32s3/procpu -d "$App\build-external" $App -- "-DDTC=$Dtc"

# Change local.conf to CONFIG_APP_MOTOR_CONTROLLER="VICTOR_SPX" for an RC PWM build
& $Python -m west build -p always -b esp32s3_devkitc/esp32s3/procpu -d "$App\build-external-frc" $App -- "-DDTC=$Dtc"

# Only after checking wiring and disconnecting the mechanism for commissioning
& $Python -m west flash -d "$App\build-external" --esp-device COM5
```

Replace `COM5` with the actual board's port. Serial console: **115200 baud**.
The images are `$App\build-external\zephyr\zephyr.bin` and
`$App\build-external-frc\zephyr\zephyr.bin`; prefer `west flash` so the runner
supplies the correct bootloader/image offsets. After changing motor selection in an existing
build directory, use `-p always` so the overlay and configuration are regenerated.

For an **already configured repository-local** workspace only, open the
`Zephyr (PowerShell)` profile in `Z-Valve` instead. From that application
directory, the equivalent BTS build/flash commands are:

```powershell
python -m west build -p always -b esp32s3_devkitc/esp32s3/procpu -d build .
python -m west flash -d build --esp-device COM5
```

That layout uses the app-local `.venv`, the sibling `..\zephyr`, and the
repository's root `.west`. Do not use these relative commands in the
external workspace. Its RC variant directory can remain `build-frc`.

| `CONFIG_APP_MOTOR_CONTROLLER` value | Model | Reverse / neutral / forward (us) | Nominal frame |
| --- | --- | --- | --- |
| `BTS7960` (default) | BTS7960 | Two duty outputs, 20 kHz, separate enable | 50 us |
| `VICTOR_SPX` | Victor SPX | 997 / 1500 / 2004 | 5050 us |
| `TALON_SRX` | Talon SRX | 997 / 1500 / 2004 | 5050 us |
| `JAGUAR_GREY` | Jaguar Grey (MDL-BDC24) | 697 / 1507 / 2310 | 5050 us |
| `VICTOR_888` | Victor 888 | 1026 / 1507 / 2027 | 10100 us |
| `VICTOR_884` | Victor 884; verify calibration | 1026 / 1507 / 2027 | 10100 us |

All four controllers must match. Constants for travel/reversal/report timing
and BTS duty are in [include/motor_output.h](include/motor_output.h).
Optional compile definitions retain `FRC_PWM_PERIOD_US`, `FRC_PWM_REVERSE_US`,
`FRC_PWM_NEUTRAL_US`, `FRC_PWM_FORWARD_US`, and `FRC_PWM_SPEED_PERMILLE`.
For example, append
`'-DEXTRA_CPPFLAGS=-DFRC_PWM_SPEED_PERMILLE=500 -DFRC_PWM_PERIOD_US=20000'`
to a fresh RC build's CMake arguments to halve pulse excursion at 50 Hz.

Zephyr's LEDC driver chooses hardware resolution rather than Arduino's fixed
8/16 bits. RC frame frequencies are rounded to the same integer rates
(198/99 Hz for the presets); pulses are expressed in nanoseconds using that
frame. Clock/divider quantization still requires oscilloscope verification.
Default BTS full drive is 100% duty; stopped is both PWM legs low and enable
low (coast), while RC stopped is continuous neutral.

## MQTT contract

The default root and client ID are both `heater-valves`:

| Topic | Payload |
| --- | --- |
| `heater-valves/valve1` ... `/valve4` | `open`, `on`, `close`, `off` |
| `heater-valves/valve1/set` ... `/valve4/set` | Same commands |
| `heater-valves/valve1/state` ... `/valve4/state` | `open`, `closed`, `opening`, `closing`, `unknown`, `error` |
| `heater-valves/availability` | `online`, `offline` |

Both limits active reports `error`; otherwise open/closed limits take precedence.
With neither active, report motion or `unknown` when stopped/reversing/timed out.
Commands are length-delimited; uppercase, whitespace, embedded NULs, wrong valve
numbers, and unrelated topics do not move a motor.

**Never retain commands.** As in the original, retained command replays are
accepted and can move a valve after reconnect; Zephyr additionally logs a warning.
Clear old retained commands before commissioning. Payloads up to 512 bytes are
fully consumed even when ignored (including this client's own availability
publication); larger payloads or unexpected QoS disconnect and retry rather
than leaving unread bytes in the stream. Fragmented payloads have a one-second
completion deadline. Unanswered MQTT pings have a ten-second response deadline.

MQTT is **plain TCP**, matching the Arduino firmware: no TLS or credential
encryption. Use a trusted, isolated network and appropriate broker ACLs.
Availability is subject to broker keepalive/will detection delay, not an
instantaneous guarantee of device or motor health.

## Verification

Host tests need CMake, Ninja and a C++17 compiler, but no Zephyr installation.
Run these **from the application directory**, not from `zephyrproject`:

```powershell
cmake -S tests -B build-tests -G Ninja
cmake --build build-tests
ctest --test-dir build-tests --output-on-failure
```

They exercise the motion/limit matrix, reversal and timeout boundaries,
32-bit time rollover, independent valves, exact MQTT parsing/topic matching,
and electrical output mapping for all six motor models plus half-speed RC PWM.

Cross-compilation does not validate physical pin levels, motor direction,
limit wiring, Wi-Fi association, or a live broker. Commission with motors
disconnected: verify neutral/disabled outputs at boot/reset, every limit and
conflict, reversal timing, travel timeout, network-loss supervision, reconnect
snapshots, and the retained will before connecting the mechanism.

## Addendum: complete Windows setup from scratch

This is a self-contained setup for a new Windows machine. It deliberately
creates the **external layout** used above, not another environment inside
the application. No external getting-started guide is needed. Do not use
this section to erase or restart an already working workspace.

Copy and paste its command blocks into PowerShell as described in
[How to run the command blocks](#how-to-run-the-command-blocks). Do not create
a batch file or paste all the setup blocks at once.

### A1. Install Windows host tools

Use Windows with WinGet available. Run each command separately, skipping
tools already installed:

```powershell
winget install --id Git.Git --exact --source winget
winget install --id Microsoft.PowerShell --exact --source winget
winget install --id Python.Python.3.12 --exact --source winget
winget install --id 7zip.7zip --exact --source winget
winget install --id oss-winget.dtc --exact --source winget --scope user
```

VS Code is optional for command-line building. If wanted, install it with:

```powershell
winget install --id Microsoft.VisualStudioCode --exact --source winget
```

Close existing terminals and restart VS Code after installation so new
processes inherit the updated `PATH`. Open **PowerShell 7** (`pwsh -NoProfile`),
not Command Prompt. PowerShell 7.4 or newer is recommended for the native
command error handling used in the examples. Verify:

```powershell
$PSVersionTable.PSVersion
git --version
py -3.12 --version
Get-Command pwsh
```

Python 3.12 is the tested choice. Zephyr 4.5.0 needs Python 3.12+, CMake
3.20.5+, Ninja, and DTC 1.4.6+. CMake and Ninja will be installed into the
external Python environment, so separate system installations are unnecessary.
The ESP32-S3 compiler comes from the Zephyr SDK, not MSVC or ESP-IDF.

### A2. Create the external Python environment, then initialize Zephyr 4.5.0

Keep using this same PowerShell window. The target is
`%USERPROFILE%\zephyrproject`; if you need a different name to preserve an
existing workspace, change `$ZephyrWorkspace` before running the block and
use it consistently afterward.

```powershell
$ErrorActionPreference = "Stop"
$PSNativeCommandUseErrorActionPreference = $true
$ZephyrWorkspace = Join-Path $env:USERPROFILE "zephyrproject"
$Python = Join-Path $ZephyrWorkspace ".venv\Scripts\python.exe"
if (Test-Path "$ZephyrWorkspace\.west\config") {
    throw "Workspace already initialized. Use the existing-workspace instructions instead."
}
if (-not (Test-Path $Python)) {
    py -3.12 -m venv "$ZephyrWorkspace\.venv"
}
if (-not (Test-Path $Python)) { throw "Python environment was not created: $Python" }
$env:PATH = "$(Split-Path $Python);$env:PATH"
& $Python -m pip install --upgrade pip
& $Python -m pip install "west>=0.14.0" "cmake>=3.20.5" ninja
& $Python -c "import sys; print(sys.executable); print(sys.prefix)"
& $Python -m west --version
```

Expected: the Python executable and prefix are both inside the external
`.venv`. The `&` is PowerShell's invocation operator; keep it when invoking
the executable stored in `$Python`. Do not copy an environment from another
machine.

Initialize from your home directory, not from an application checkout:

```powershell
Set-Location $env:USERPROFILE
if (Test-Path Env:\ZEPHYR_BASE) { Remove-Item Env:\ZEPHYR_BASE }
& $Python -m west init -m https://github.com/zephyrproject-rtos/zephyr --mr v4.5.0 -o=--depth=1 $ZephyrWorkspace
Set-Location $ZephyrWorkspace
$ZephyrBase = Join-Path $ZephyrWorkspace "zephyr"
$env:ZEPHYR_BASE = $ZephyrBase
$env:ZEPHYR_TOOLCHAIN_VARIANT = "zephyr"
& $Python -m west topdir
git -C $ZephyrBase rev-parse HEAD
& $Python -m west update hal_espressif mbedtls tf-psa-crypto
& $Python -m pip install -r "$ZephyrBase\scripts\requirements-base.txt"
& $Python -m pip install -r "$ZephyrWorkspace\modules\hal\espressif\zephyr\requirements.txt"
& $Python -m west manifest --validate
```

Expected: `west topdir` is `$ZephyrWorkspace` and Zephyr's commit is
`41d10b59730406bbab4ca9f1fbbe9e96aafb4966`. The `--mr v4.5.0` argument is
essential; omitting it can select an incompatible development version.
The shallow clone option limits history downloaded. Only the three required
module projects are updated; a full `west update` is unnecessary for this app.

### A3. Clone the application separately

Working directory can remain `$ZephyrWorkspace`; the clone destination is
absolute. This example creates `%USERPROFILE%\source\boilerControl`.
Change `$Repo` if you prefer another short local path:

```powershell
$Repo = Join-Path $env:USERPROFILE "source\boilerControl"
if (Test-Path $Repo) { throw "Destination already exists. Use your existing checkout instead." }
New-Item -ItemType Directory -Path (Split-Path $Repo) -Force
git clone https://github.com/nbxmike/boilerControl.git $Repo
$App = Join-Path $Repo "Z-Valve"
Get-Item "$App\CMakeLists.txt"
Write-Output "Repository root: $Repo"
Write-Output "Zephyr workspace: $ZephyrWorkspace"
```

Expected: two **different** directories, with `Z-Valve` under `$Repo` and
Zephyr/its Python environment under `$ZephyrWorkspace`. Do not clone another
Zephyr inside `$Repo`, run `west init -l .` in `Z-Valve`, or try
`Set-Location .\Z-Valve` from `zephyrproject`.

### A4. Finish and verify the build

Go to [Use an existing external Zephyr workspace](#use-an-existing-external-zephyr-workspace)
in this same document. Run its blocks 1-5, entering the repository root
printed in A3 when prompted. They provide all remaining commands:
dependency/hash checks, binary-library downloads, explicit DTC selection,
SDK 1.0.1/ESP32-S3 installation, private configuration, and a pristine build.
Repeated package/update steps are safe; no second initialization is performed.

A machine is ready only when the build succeeds and its CMake cache shows
the intended Zephyr/Python/DTC paths. A sample build in `zephyrproject`,
or an old `zephyr.bin` in the application, is not that verification.

### Troubleshooting both external setup routes

- **Variables disappear in a new terminal:** that is normal. Run block 1
  again, restore `$env:ZEPHYR_SDK_INSTALL_DIR = $Sdk` and the tool `PATH`
  from block 4, or use the explicit external VS Code profile/task.
- **west says already initialized:** stop rerunning `init`. Check `topdir`
  from the intended workspace and use `update`. Do not delete `.west`.
- **west selects the repository instead of `zephyrproject`:** run
  `Set-Location $ZephyrWorkspace` before workspace-management commands.
  `ZEPHYR_BASE` alone cannot override workspace discovery from the current
  directory. For building, stay in `$ZephyrWorkspace` and use the `topdir`
  check in block 5. The absolute application path is passed separately to `build`.
- **Wrong west workspace or a path/drive error:** run workspace commands
  and builds from `$ZephyrWorkspace`; verify block 5's `west topdir` check
  before building. Do not use the repository directory as the working directory.
- **DTC is found but fails with a missing DLL:** use the WinGet compiler's
  full `$Dtc` path and `-- "-DDTC=$Dtc"` on the pristine build. Do not reuse
  a cached path to a broken MinGW compiler.
- **SDK installation fails because `7z` is missing:** install 7-Zip, put
  its directory on `PATH`, verify `Get-Command 7z`, then retry. If the SDK
  directory already exists from a partial installation, run its setup
  commands in the configured terminal:

  ```powershell
  & "$Sdk\setup.cmd" /c
  & "$Sdk\setup.cmd" /t xtensa-espressif_esp32s3_zephyr-elf
  ```

  Verify the compiler and SDK listing afterward using block 4.
- **Git connection reset:** fix network/VPN/proxy access, then retry the
  failed command. A successful init followed by a failed update does not
  require reinitializing. Do not disable Git TLS verification.
- **Python download certificate errors:** Windows may trust an organization's
  CA while Python Requests does not. With Windows trust configured correctly,
  run from `$ZephyrWorkspace`:

  ```powershell
  & $Python -m pip install truststore
  & $Python -c "import truststore; truststore.inject_into_ssl(); from west.app.main import main; main()" blobs fetch hal_espressif
  & $Python -c "import truststore; truststore.inject_into_ssl(); from west.app.main import main; main()" sdk install --version 1.0.1 --install-dir $Sdk --gnu-toolchains xtensa-espressif_esp32s3_zephyr-elf
  ```

  Run only the download command that failed. This uses the Windows certificate
  store and keeps verification enabled; it does not configure Git or affect
  subprocesses. If pip itself cannot download, obtain your organization's
  approved CA bundle/proxy settings; `PIP_CERT` can point to that CA bundle.
  Do not use `--trusted-host` or turn off certificate verification.
- **PowerShell blocks profile activation:** the manual instructions and
  external process task do not require `Activate.ps1`; they use the exact
  interpreter and set `PATH`. If you want the activation-based terminal
  profile, use an execution policy permitted by your organization rather
  than disabling security controls.

### What was verified

Validation on Windows included creating a new external Python environment,
initializing Zephyr 4.5.0 from GitHub, fetching the pinned Espressif HAL,
mbedTLS, and TF-PSA-Crypto repositories, installing their Python requirements,
and fetching all 79 Espressif blobs with successful hash checks. A pristine
ESP32-S3 build completed using Zephyr 4.5.0, SDK 1.0.1, and esptool 5.6.0.
The application checkout was outside the external workspace and also had its
own `.west`; the build was run from the external workspace and its cache
selected the expected Zephyr, Python, and DTC paths.

The repository-local workspace was also updated to the same Zephyr, HAL,
mbedTLS, and TF-PSA-Crypto revisions and built successfully with the existing
private `local.conf`. Its cache selected the repository-local Zephyr and
Python paths. SDK 1.0.1 and existing Windows host tools were reused, not
uninstalled and reinstalled.

The VS Code tasks, another machine, a fresh Windows OS, and physical hardware
were not tested. Git/WinGet/PyPI/SDK downloads depend on the target machine's
network and trust configuration. Follow each check above on that machine
before flashing.
