# FRC PWM References

Downloaded 2026-11-12 from WPILib release **v2024.3.2**. The original source
files retain their notices; [WPILib-LICENSE.txt](WPILib-LICENSE.txt) is the
upstream license. These files are reference material, not compiled firmware.

Upstream release: https://github.com/wpilibsuite/allwpilib/tree/v2024.3.2

| Saved file | Upstream path within the release | Used for |
| --- | --- | --- |
| [PWMVictorSPX.cpp](PWMVictorSPX.cpp) | `wpilibc/src/main/native/cpp/motorcontrol/PWMVictorSPX.cpp` | Victor SPX pulse bounds |
| [PWMTalonSRX.cpp](PWMTalonSRX.cpp) | `wpilibc/src/main/native/cpp/motorcontrol/PWMTalonSRX.cpp` | Talon SRX pulse bounds |
| [Jaguar.cpp](Jaguar.cpp) | `wpilibc/src/main/native/cpp/motorcontrol/Jaguar.cpp` | Jaguar / MDL-BDC24 pulse bounds |
| [Jaguar.h](Jaguar.h) | `wpilibc/src/main/native/include/frc/motorcontrol/Jaguar.h` | Jaguar calibration caveat |
| [Victor.cpp](Victor.cpp) | `wpilibc/src/main/native/cpp/motorcontrol/Victor.cpp` | Legacy Victor pulse bounds |
| [Victor.h](Victor.h) | `wpilibc/src/main/native/include/frc/motorcontrol/Victor.h` | Victor 888 and 884 compatibility/calibration caveat |
| [PWM.h](PWM.h) | `wpilibc/src/main/native/include/frc/PWM.h` | Pulse timing and frame multipliers |
| [WPILib-LICENSE.txt](WPILib-LICENSE.txt) | `LICENSE.md` | Upstream redistribution terms |

## Applied Timing

| Controller | Reverse (us) | Neutral (us) | Forward (us) | Nominal frame (us) | LEDC rate |
| --- | --- | --- | --- | --- | --- |
| Victor SPX / Talon SRX | 997 | 1500 | 2004 | 5050 | 198 Hz |
| TI MDL-BDC24 / Jaguar | 697 | 1507 | 2310 | 5050 | 198 Hz |
| Victor 888 / Victor 884 | 1026 | 1507 | 2027 | 10100 | 99 Hz |

The legacy Victor bounds are optimized for the 888; WPILib explicitly notes
that the 884 may require calibration. Jaguar calibration is also recommended
when symmetry or full output is incorrect. Stored calibration and controller
settings can override assumptions about neutral, endpoints, and braking.

One global `CONFIG_APP_MOTOR_CONTROLLER` selection in `local.conf` applies the
interface, pin map, pulse bounds, and frame timing to all four motors. All four
controllers must be the same make/model; mixed models are not supported.

The firmware now follows the saved WPILib repeat-rate presets: these CTRE/Jaguar
sources select 1X (5.05 ms), while legacy Victor selects 2X (10.10 ms).
WPILib also documents 4X (20.20 ms). The ESP32 LEDC API requires integer Hz,
so firmware selects the nearest rates, 198 Hz (approximately 5050.505 us) and
99 Hz (approximately 10101.010 us). Duty conversion uses the configured Hz;
hardware divider/clock precision adds a small additional timing error. The
original 20 ms baseline remains available through `FRC_PWM_PERIOD_US=20000`.

These are reference implementation presets, not manufacturer certification of
every controller at every frame rate. Verify the selected signal and
loss-of-signal behavior on actual hardware before coupling a valve, and consult
its own manual for voltage, power wiring, PWM mode, and calibration instructions.

Manufacturer pages/PDFs attempted during research could not be extracted, so
they are not represented here as verified references. No firmware logic or
credentials were taken from external sources.