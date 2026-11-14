#pragma once

#include "valve_control.h"

#define MOTOR_CONTROLLER_BTS7960 1
#define MOTOR_CONTROLLER_VICTOR_SPX 2
#define MOTOR_CONTROLLER_TALON_SRX 3
#define MOTOR_CONTROLLER_JAGUAR_GREY 4
#define MOTOR_CONTROLLER_VICTOR_888 5
#define MOTOR_CONTROLLER_VICTOR_884 6

#ifndef MOTOR_CONTROLLER
#define MOTOR_CONTROLLER MOTOR_CONTROLLER_BTS7960
#endif

#if MOTOR_CONTROLLER != MOTOR_CONTROLLER_BTS7960 && \
    MOTOR_CONTROLLER != MOTOR_CONTROLLER_VICTOR_SPX && \
    MOTOR_CONTROLLER != MOTOR_CONTROLLER_TALON_SRX && \
    MOTOR_CONTROLLER != MOTOR_CONTROLLER_JAGUAR_GREY && \
    MOTOR_CONTROLLER != MOTOR_CONTROLLER_VICTOR_888 && \
    MOTOR_CONTROLLER != MOTOR_CONTROLLER_VICTOR_884
#error "Select a supported MOTOR_CONTROLLER_* value"
#endif

#ifndef FRC_PWM_SPEED_PERMILLE
#define FRC_PWM_SPEED_PERMILLE 1000
#endif

namespace motor {

constexpr bool kBts7960 = MOTOR_CONTROLLER == MOTOR_CONTROLLER_BTS7960;

#if MOTOR_CONTROLLER == MOTOR_CONTROLLER_JAGUAR_GREY
constexpr uint32_t kDefaultReverseUs = 697; // Reverse-command pulse width (us).
constexpr uint32_t kDefaultNeutralUs = 1507; // Neutral/stop pulse width (us).
constexpr uint32_t kDefaultForwardUs = 2310; // Forward-command pulse width (us).
constexpr uint32_t kDefaultPeriodUs = 5050; // PWM frame: 5.05 ms, approximately 198 Hz.
#elif MOTOR_CONTROLLER == MOTOR_CONTROLLER_VICTOR_888 || \
      MOTOR_CONTROLLER == MOTOR_CONTROLLER_VICTOR_884
constexpr uint32_t kDefaultReverseUs = 1026; // Reverse-command pulse width (us).
constexpr uint32_t kDefaultNeutralUs = 1507; // Neutral/stop pulse width (us).
constexpr uint32_t kDefaultForwardUs = 2027; // Forward-command pulse width (us).
constexpr uint32_t kDefaultPeriodUs = 10100; // PWM frame: 10.1 ms, approximately 99 Hz.
#else
constexpr uint32_t kDefaultReverseUs = 997; // Reverse-command pulse width (us).
constexpr uint32_t kDefaultNeutralUs = 1500; // Neutral/stop pulse width (us).
constexpr uint32_t kDefaultForwardUs = 2004; // Forward-command pulse width (us).
constexpr uint32_t kDefaultPeriodUs = 5050; // PWM frame: 5.05 ms, approximately 198 Hz.
#endif

#ifndef FRC_PWM_PERIOD_US
#define FRC_PWM_PERIOD_US motor::kDefaultPeriodUs
#endif
#ifndef FRC_PWM_REVERSE_US
#define FRC_PWM_REVERSE_US motor::kDefaultReverseUs
#endif
#ifndef FRC_PWM_NEUTRAL_US
#define FRC_PWM_NEUTRAL_US motor::kDefaultNeutralUs
#endif
#ifndef FRC_PWM_FORWARD_US
#define FRC_PWM_FORWARD_US motor::kDefaultForwardUs
#endif

constexpr uint32_t kPwmFrequencyHz = kBts7960 ? 20000 :
    (1000000UL + FRC_PWM_PERIOD_US / 2) / FRC_PWM_PERIOD_US;
// ESP32 LEDC selects integer Hz; use that same frame when calculating pulse duty.
constexpr uint32_t kPeriodNs = 1000000000UL / kPwmFrequencyHz;
constexpr uint32_t kReverseUs = FRC_PWM_REVERSE_US;
constexpr uint32_t kNeutralUs = FRC_PWM_NEUTRAL_US;
constexpr uint32_t kForwardUs = FRC_PWM_FORWARD_US;
constexpr uint32_t kSpeedPermille = FRC_PWM_SPEED_PERMILLE;
constexpr uint32_t kReversalDelayMs = 100;
constexpr uint32_t kMaxTravelMs = 120000;
constexpr uint32_t kStatusIntervalMs = 60000;
constexpr unsigned kValveCount = 4;
constexpr uint32_t kMotorDuty = 255;
constexpr uint32_t kMotorPulseNs = kPeriodNs * kMotorDuty / 255;

constexpr const char *kControllerName =
#if MOTOR_CONTROLLER == MOTOR_CONTROLLER_BTS7960
    "BTS7960";
#elif MOTOR_CONTROLLER == MOTOR_CONTROLLER_VICTOR_SPX
    "Victor SPX";
#elif MOTOR_CONTROLLER == MOTOR_CONTROLLER_TALON_SRX
    "Talon SRX";
#elif MOTOR_CONTROLLER == MOTOR_CONTROLLER_JAGUAR_GREY
    "Jaguar Grey (MDL-BDC24)";
#elif MOTOR_CONTROLLER == MOTOR_CONTROLLER_VICTOR_888
    "Victor 888";
#else
    "Victor 884";
#endif

static_assert(kMotorDuty > 0 && kMotorDuty <= 255, "Invalid BTS7960 duty");
static_assert(FRC_PWM_PERIOD_US >= 5000 && FRC_PWM_PERIOD_US <= 30000,
              "RC PWM frame must be 5-30 ms");
static_assert(kReverseUs >= 500 && kReverseUs < kNeutralUs &&
              kNeutralUs < kForwardUs && kForwardUs <= 2500 &&
              kForwardUs * 1000 < FRC_PWM_PERIOD_US * 1000, "Invalid RC PWM calibration");
static_assert(kSpeedPermille > 0 && kSpeedPermille <= 1000, "Invalid RC PWM speed");

struct Output {
    uint32_t primaryNs;
    uint32_t secondaryNs;
    bool enabled;
};

/*
 * Map logical valve motion and opening direction to motor-driver outputs.
 * Args:
 * motion - requested motion.
 * openClockwise - whether opening is clockwise.
 * Returns: PWM pulse widths in ns and whether the output should be enabled.
 */
inline Output outputFor(valve::Motion motion, bool openClockwise) {
    const bool clockwise = (motion == valve::Motion::Opening) == openClockwise;
    if (kBts7960) {
        if (motion == valve::Motion::Stopped) return {0, 0, false};
        return {clockwise ? kMotorPulseNs : 0, clockwise ? 0 : kMotorPulseNs, true};
    }
    uint32_t pulseUs = kNeutralUs;
    if (motion != valve::Motion::Stopped) {
        pulseUs = clockwise
            ? kNeutralUs + (kForwardUs - kNeutralUs) * kSpeedPermille / 1000
            : kNeutralUs - (kNeutralUs - kReverseUs) * kSpeedPermille / 1000;
    }
    return {pulseUs * 1000, 0, motion != valve::Motion::Stopped};
}

}
