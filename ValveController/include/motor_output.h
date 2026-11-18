#pragma once

#include "valve_config.h"
#include "valve_control.h"

namespace motor {

// BTS uses both duties plus enable; FRC uses primaryDuty only and sends neutral at rest.
struct Output {
    uint32_t primaryDuty;
    uint32_t secondaryDuty;
    bool enabled;
};

#if MOTOR_DRIVER == MOTOR_DRIVER_FRC_PWM
// Convert pulse microseconds to LEDC timer counts using configured Hz and nearest rounding.
// A 64-bit intermediate keeps the frequency/resolution multiplication from overflowing.
constexpr uint32_t pulseToDuty(uint32_t pulseUs) {
    return static_cast<uint32_t>((static_cast<uint64_t>(pulseUs) *
    hardware::kPwmFrequencyHz * (1UL << hardware::kPwmResolutionBits) + 500000UL) /
    1000000UL);
}
#endif

// Map logical opening/closing to electrical direction without changing MQTT or limit semantics.
inline Output outputFor(valve::Motion motion, hardware::Direction openDirection) {
    const bool clockwise = (motion == valve::Motion::Opening) ==
                           (openDirection == hardware::Direction::Clockwise);
#if MOTOR_DRIVER == MOTOR_DRIVER_BTS7960
    if (motion == valve::Motion::Stopped) {
        return {0, 0, false};
    }
    return {clockwise ? hardware::kMotorDuty : 0,
            clockwise ? 0 : hardware::kMotorDuty, true};
#else
    // Scale each calibrated endpoint's distance from neutral; this is not speed feedback.
    uint32_t pulseUs = hardware::kNeutralUs;
    if (motion != valve::Motion::Stopped) {
        pulseUs = clockwise
            ? hardware::kNeutralUs + (hardware::kForwardUs - hardware::kNeutralUs) *
                hardware::kSpeedPermille / 1000
            : hardware::kNeutralUs - (hardware::kNeutralUs - hardware::kReverseUs) *
                hardware::kSpeedPermille / 1000;
    }
    return {pulseToDuty(pulseUs), 0, motion != valve::Motion::Stopped};
#endif
}

}