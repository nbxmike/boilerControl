# Digital Isolation Options

Manufacturer pages consulted on 2026-11-12. This is a selection aid, not a
completed or safety-certified hardware design. No firmware changes are needed
for a noninverting logic isolator with suitable voltage levels and timing.
Distributor stock and small-quantity prices were not verified.

## Shortlist

| Device | Forward channels | Isolated power included | Selection notes |
| --- | --- | --- | --- |
| [TI ISO6440F](https://www.ti.com/product/ISO6440) | 4 | No | Newer family highlighted by TI as an alternative to ISO7740/ISO6740. 2.25-5.5 V supplies, default-low F variant. Example wide-SOIC part: ISO6440FDWR. |
| [TI ISO7740F](https://www.ti.com/product/ISO7740) | 4 | No | Established 2.25-5.5 V level-translating isolator; default-low F variant. Example: ISO7740FDWR. |
| [ADI ADuM340E0](https://www.analog.com/en/products/adum340e.html) | 4 | No | 2.25-5.5 V level translation; E0 is fail-safe low, E1 fail-safe high. |
| [TI ISOW7740F](https://www.ti.com/product/ISOW7740) | 4 | Yes | Integrated DC-DC converter and separate logic supply. For 5 V isolated output, use a supported 5 V converter input configuration; ESP32-side VIO can remain 3.3 V. |
| [ADI ADuM6420A](https://www.analog.com/en/products/adum6420a.html) | 4 | Yes | isoPower alternative with integrated converter. Confirm exact suffix, default-output behavior, logic supplies, converter input/output mode, and layout from the datasheet before selection. |

The first three replace both signal-level translation and the signal path across
an isolation barrier, but require power on both sides. For 3.3-to-5 V operation,
power their input side from ESP32 3.3 V and their output side from a suitable
motor-side 5 V supply. They are functional alternatives, not pin-compatible
replacements for 74AHCT125/244. Normal bypass capacitors, enable-pin biasing,
pull-downs, and PCB isolation clearances are still required.

All shortlisted signal paths are much faster than the firmware's 20 kHz BTS7960
PWM or approximately 99/198 Hz FRC PWM. Maintain a noninverting signal path;
check pulse-width distortion and output loading rather than pursuing high data
rates for their own sake.

## Low-Assembly Approach

Prefer an assembled breakout/evaluation module over a bare IC when avoiding PCB
design and fine-pitch soldering. Useful manufacturer board families include:

- [ADI EVAL-ADUM34XEEBZ](https://www.analog.com/en/resources/evaluation-hardware-and-software/evaluation-boards-kits/eval-adum34xeebz.html): breakaway sections for the ADuM340E/341E/342E family, plus an explicitly unpopulated U4 footprint. Check the fitted 4-forward device's default-output suffix and configure its enable pins; the product page says enable pull-ups are not fitted by default.
- [TI ISO64XX-WB-EVM](https://www.ti.com/tool/ISO64XX-WB-EVM): breakaway evaluation platform for several ISO64xx packages. Check the bill of materials for the actual fitted device before purchasing; family compatibility does not guarantee a populated ISO6440F section.
- [TI DIGI-ISO-EVM](https://www.ti.com/tool/DIGI-ISO-EVM): universal evaluation platform. Do not assume it includes the desired isolator; confirm population before choosing it as a solder-free solution.

Many common ISO7741/ISOW7741 evaluation boards have three forward channels and
one reverse channel, not four forward channels. They are not a single-board
replacement for four independent FRC outputs. Likewise, I2C and USB isolator
modules are not substitutes for ordinary unidirectional PWM/GPIO isolators.

## Application Constraints

- Four FRC PWM outputs require four forward channels: one four-channel device if all motor-side signal returns may share a domain.
- Four BTS7960 boards require twelve forward channels: RPWM, LPWM, and the tied enable pair per motor. Three four-channel devices suffice if output-side ground/power can be shared.
- Four channels usually share their side's ground. They isolate the ESP32 domain from the motor domain, not the four motors from one another. Independently isolated motor domains require separate barriers and power arrangements.
- Do not carry the existing shared-ground instructions across an added isolation barrier. Output ground connects to the motor controller's signal return; ESP32 ground stays on the input side. A common-ground power supply, USB connection, grounded scope, or other wiring path can bypass isolation.
- The eight limit inputs are not isolated by adding isolators only to motor outputs. Keep dry-contact wiring in the ESP32 domain or provide appropriate isolation for motor-domain sensor signals.
- Choose default-low behavior and retain suitable pull-downs, especially on BTS7960 enable inputs. Default-low is not a guarantee of safe behavior for every power-up, reset, disabled-output, or supply-loss condition; verify the specific truth tables and complete hardware behavior.
- For FRC controllers, low means loss of pulses, not neutral. Stopping then depends on the motor controller's signal-loss timeout. A signal isolator does not synthesize neutral PWM or implement an emergency stop.
- Integrated isolated power is for the interface and other explicitly budgeted small loads, not the motor. Respect converter mode/current limits, bypassing, and EMI/layout recommendations; do not parallel isolated converter outputs without explicit datasheet permission.
- An isolator's push-pull outputs are logic outputs, not long-cable industrial line drivers. Check the actual controller input and cable load.

## Saved Material

The following manufacturer PDF downloads were checked for valid PDF signatures:

- [TI-ISO6440.pdf](TI-ISO6440.pdf), from https://www.ti.com/lit/gpn/iso6440
- [TI-ISO7740.pdf](TI-ISO7740.pdf), from https://www.ti.com/lit/gpn/iso7740
- [TI-ISOW7740.pdf](TI-ISOW7740.pdf), from https://www.ti.com/lit/gpn/isow7740

ADI product and evaluation-board HTML pages were accessible, but their datasheet
and user-guide PDF downloads failed on the current network. Use the manufacturer
links above to retrieve them on an appropriate network. Saved manufacturer
documents retain their original notices and ownership.