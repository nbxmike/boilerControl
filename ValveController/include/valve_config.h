#pragma once

#include <stdint.h>

// Select one make/model for all four motors; mixed controller models are unsupported.
// A build flag can override the default MOTOR_CONTROLLER selection below.
#define MOTOR_CONTROLLER_BTS7960 1
#define MOTOR_CONTROLLER_VICTOR_SPX 2
#define MOTOR_CONTROLLER_TALON_SRX 3
#define MOTOR_CONTROLLER_MDL_BDC24 4
#define MOTOR_CONTROLLER_VICTOR_888 5
#define MOTOR_CONTROLLER_VICTOR_884 6
#ifndef MOTOR_CONTROLLER
#define MOTOR_CONTROLLER MOTOR_CONTROLLER_BTS7960
#endif

// Derive the electrical interface from the model; reject obsolete, conflicting flags.
#if defined(MOTOR_DRIVER) || defined(FRC_PWM_MODEL)
#error "Select MOTOR_CONTROLLER only; MOTOR_DRIVER and FRC_PWM_MODEL are obsolete selection flags"
#endif

#define MOTOR_DRIVER_BTS7960 1
#define MOTOR_DRIVER_FRC_PWM 2
#if MOTOR_CONTROLLER == MOTOR_CONTROLLER_BTS7960
#define MOTOR_DRIVER MOTOR_DRIVER_BTS7960
#elif MOTOR_CONTROLLER >= MOTOR_CONTROLLER_VICTOR_SPX && MOTOR_CONTROLLER <= MOTOR_CONTROLLER_VICTOR_884
#define MOTOR_DRIVER MOTOR_DRIVER_FRC_PWM
#else
#error "Unsupported MOTOR_CONTROLLER"
#endif

// Global RC pulse excursion: 1000 means full calibrated output, 500 means half excursion.
#ifndef FRC_PWM_SPEED_PERMILLE
#define FRC_PWM_SPEED_PERMILLE 1000
#endif

namespace hardware {

enum class Direction : uint8_t { Clockwise, Counterclockwise };

// Motor outputs depend on the interface; limits and opening direction are per valve.
struct ValvePins {
#if MOTOR_DRIVER == MOTOR_DRIVER_BTS7960
    uint8_t rightPwm;
    uint8_t leftPwm;
    uint8_t enable;
#else
    uint8_t signalPwm;
#endif
    uint8_t openLimit;
    uint8_t closedLimit;
    Direction openDirection;
};

// Rows are valves 1-4 and use GPIO numbers, not physical header positions.
// BTS rows: RPWM, LPWM, enable, open limit, closed limit, opening direction.
// FRC rows: signal, open limit, closed limit, opening direction; valve 3 uses GPIO14.
constexpr ValvePins kValves[] = {
#if MOTOR_DRIVER == MOTOR_DRIVER_BTS7960
    {32, 33, 25, 36, 39, Direction::Clockwise},
    {26, 27, 14, 34, 35, Direction::Clockwise},
    {23, 22, 21, 13, 15, Direction::Clockwise},
    {19, 18, 17, 16,  4, Direction::Clockwise},
#else
    {32, 36, 39, Direction::Clockwise},
    {26, 34, 35, Direction::Clockwise},
    {23, 13, 14, Direction::Clockwise},
    {19, 16,  4, Direction::Clockwise},
#endif
};

constexpr uint8_t kValveCount = sizeof(kValves) / sizeof(kValves[0]);
#if MOTOR_DRIVER == MOTOR_DRIVER_BTS7960
// BTS7960 uses high-frequency duty PWM plus a separate bridge-enable output.
constexpr uint32_t kPwmFrequencyHz = 20000;
constexpr uint8_t kPwmResolutionBits = 8;
constexpr uint32_t kMotorDuty = 255;
static_assert(kMotorDuty > 0 && kMotorDuty < (1U << kPwmResolutionBits), "Invalid PWM duty");
#else
// WPILib model presets: reverse/neutral/forward pulse widths and nominal frame period.
// Legacy Victor 884 shares the 888 preset but may require controller calibration.
#if MOTOR_CONTROLLER == MOTOR_CONTROLLER_VICTOR_SPX || MOTOR_CONTROLLER == MOTOR_CONTROLLER_TALON_SRX
constexpr uint32_t kDefaultReverseUs = 997;
constexpr uint32_t kDefaultNeutralUs = 1500;
constexpr uint32_t kDefaultForwardUs = 2004;
constexpr uint32_t kDefaultPwmPeriodUs = 5050;
#elif MOTOR_CONTROLLER == MOTOR_CONTROLLER_MDL_BDC24
constexpr uint32_t kDefaultReverseUs = 697;
constexpr uint32_t kDefaultNeutralUs = 1507;
constexpr uint32_t kDefaultForwardUs = 2310;
constexpr uint32_t kDefaultPwmPeriodUs = 5050;
#elif MOTOR_CONTROLLER == MOTOR_CONTROLLER_VICTOR_888 || MOTOR_CONTROLLER == MOTOR_CONTROLLER_VICTOR_884
constexpr uint32_t kDefaultReverseUs = 1026;
constexpr uint32_t kDefaultNeutralUs = 1507;
constexpr uint32_t kDefaultForwardUs = 2027;
constexpr uint32_t kDefaultPwmPeriodUs = 10100;
#endif
// Optional build flags override calibration or frame timing for all four controllers.
#ifndef FRC_PWM_PERIOD_US
#define FRC_PWM_PERIOD_US hardware::kDefaultPwmPeriodUs
#endif
#ifndef FRC_PWM_REVERSE_US
#define FRC_PWM_REVERSE_US hardware::kDefaultReverseUs
#endif
#ifndef FRC_PWM_NEUTRAL_US
#define FRC_PWM_NEUTRAL_US hardware::kDefaultNeutralUs
#endif
#ifndef FRC_PWM_FORWARD_US
#define FRC_PWM_FORWARD_US hardware::kDefaultForwardUs
#endif
constexpr uint32_t kPwmPeriodUs = FRC_PWM_PERIOD_US;
// LEDC accepts integer Hz; pulse conversion uses this rounded frequency, not nominal us.
constexpr uint32_t kPwmFrequencyHz = kPwmPeriodUs == 0 ? 0 :
    (1000000UL + kPwmPeriodUs / 2) / kPwmPeriodUs;
constexpr uint8_t kPwmResolutionBits = 16;
constexpr uint32_t kReverseUs = FRC_PWM_REVERSE_US;
constexpr uint32_t kNeutralUs = FRC_PWM_NEUTRAL_US;
constexpr uint32_t kForwardUs = FRC_PWM_FORWARD_US;
constexpr uint32_t kSpeedPermille = FRC_PWM_SPEED_PERMILLE;
// Reject timing/calibration settings that cannot form a valid bidirectional RC signal.
static_assert(kPwmPeriodUs >= 5000 && kPwmPeriodUs <= 30000,
              "PWM period must be 5-30 ms");
static_assert(kReverseUs >= 500 && kReverseUs < kNeutralUs &&
              kNeutralUs < kForwardUs && kForwardUs <= 2500 &&
              kForwardUs < kPwmPeriodUs, "Invalid RC PWM pulse bounds");
static_assert(kSpeedPermille > 0 && kSpeedPermille <= 1000, "Invalid FRC PWM speed");
#endif
// Reversal pause, maximum continuous travel, and one-valve-at-a-time status refresh.
constexpr uint32_t kReversalDelayMs = 100;
constexpr uint32_t kMaxTravelMs = 120000;
constexpr uint32_t kStatusIntervalMs = 60000;

// Supported GPIO subset for the classic ESP32-WROOM board; flash/UART pins are excluded.
constexpr bool outputCapable(uint8_t pin) {
    return pin == 4 || (pin >= 13 && pin <= 19) ||
           (pin >= 21 && pin <= 23) || (pin >= 25 && pin <= 27) ||
           pin == 32 || pin == 33;
}

constexpr bool inputCapable(uint8_t pin) {
    return outputCapable(pin) || pin == 34 || pin == 35 || pin == 36 || pin == 39;
}

// Startup validation checks direction capability and prohibits sharing any signal pin.
inline bool validPinMap() {
    bool used[40] = {};
    for (const ValvePins &pins : kValves) {
#if MOTOR_DRIVER == MOTOR_DRIVER_BTS7960
        if (!outputCapable(pins.rightPwm) || !outputCapable(pins.leftPwm) ||
            !outputCapable(pins.enable) || !inputCapable(pins.openLimit) ||
            !inputCapable(pins.closedLimit)) {
            return false;
        }
        const uint8_t signals[] = {pins.rightPwm, pins.leftPwm, pins.enable,
                                   pins.openLimit, pins.closedLimit};
    #else
        if (!outputCapable(pins.signalPwm) || !inputCapable(pins.openLimit) ||
            !inputCapable(pins.closedLimit)) {
            return false;
        }
        const uint8_t signals[] = {pins.signalPwm, pins.openLimit, pins.closedLimit};
    #endif
        for (uint8_t pin : signals) {
            if (used[pin]) return false;
            used[pin] = true;
        }
    }
    return true;
}

static_assert(kValveCount == 4, "Four valve controllers are required");
static_assert(kMaxTravelMs > 0, "A finite travel timeout is required");

}