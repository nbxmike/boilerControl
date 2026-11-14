#include <assert.h>
#include <initializer_list>
#include <stdio.h>

#include "motor_output.h"
#include "valve_config.h"
#include "valve_control.h"

using valve::Command;
using valve::Control;
using valve::Limits;
using valve::Motion;
using valve::Status;

int main() {
    assert(hardware::validPinMap());
#if MOTOR_DRIVER == MOTOR_DRIVER_FRC_PWM
    if (hardware::kPwmPeriodUs == 20000) {
        assert(hardware::kPwmFrequencyHz == 50);
        assert(motor::pulseToDuty(1000) == 3277);
        assert(motor::pulseToDuty(1500) == 4915);
        assert(motor::pulseToDuty(2000) == 6554);
    }
#if MOTOR_CONTROLLER == MOTOR_CONTROLLER_VICTOR_SPX || MOTOR_CONTROLLER == MOTOR_CONTROLLER_TALON_SRX
    assert(hardware::kDefaultReverseUs == 997 && hardware::kDefaultNeutralUs == 1500 &&
           hardware::kDefaultForwardUs == 2004);
    assert(hardware::kDefaultPwmPeriodUs == 5050);
#elif MOTOR_CONTROLLER == MOTOR_CONTROLLER_MDL_BDC24
    assert(hardware::kDefaultReverseUs == 697 && hardware::kDefaultNeutralUs == 1507 &&
           hardware::kDefaultForwardUs == 2310);
    assert(hardware::kDefaultPwmPeriodUs == 5050);
#else
    assert(hardware::kDefaultReverseUs == 1026 && hardware::kDefaultNeutralUs == 1507 &&
           hardware::kDefaultForwardUs == 2027);
    assert(hardware::kDefaultPwmPeriodUs == 10100);
#endif
    if (hardware::kPwmPeriodUs == 5050) {
        assert(hardware::kPwmFrequencyHz == 198);
        assert(motor::pulseToDuty(1500) == 19464);
    }
    if (hardware::kPwmPeriodUs == 10100) {
        assert(hardware::kPwmFrequencyHz == 99);
        assert(motor::pulseToDuty(1507) == 9778);
    }
    const motor::Output forward = motor::outputFor(Motion::Opening, hardware::Direction::Clockwise);
    const motor::Output reverse = motor::outputFor(Motion::Closing, hardware::Direction::Clockwise);
    const uint32_t forwardUs = hardware::kNeutralUs +
        (hardware::kForwardUs - hardware::kNeutralUs) * hardware::kSpeedPermille / 1000;
    const uint32_t reverseUs = hardware::kNeutralUs -
        (hardware::kNeutralUs - hardware::kReverseUs) * hardware::kSpeedPermille / 1000;
    assert(forward.primaryDuty == motor::pulseToDuty(forwardUs));
    assert(reverse.primaryDuty == motor::pulseToDuty(reverseUs));
    assert(forward.secondaryDuty == 0 && reverse.secondaryDuty == 0);
#endif
    for (const hardware::Direction direction : {hardware::Direction::Clockwise,
                                               hardware::Direction::Counterclockwise}) {
        const motor::Output stopped = motor::outputFor(Motion::Stopped, direction);
        const motor::Output opening = motor::outputFor(Motion::Opening, direction);
        const motor::Output closing = motor::outputFor(Motion::Closing, direction);
        assert(!stopped.enabled && opening.enabled && closing.enabled);
#if MOTOR_DRIVER == MOTOR_DRIVER_BTS7960
        assert(stopped.primaryDuty == 0 && stopped.secondaryDuty == 0);
        assert(opening.primaryDuty == closing.secondaryDuty);
        assert(opening.secondaryDuty == closing.primaryDuty);
        assert(opening.primaryDuty == (direction == hardware::Direction::Clockwise
            ? hardware::kMotorDuty : 0));
#else
        assert(stopped.primaryDuty == motor::pulseToDuty(hardware::kNeutralUs));
        assert(stopped.primaryDuty > 0);
        assert((opening.primaryDuty > stopped.primaryDuty) ==
               (direction == hardware::Direction::Clockwise));
        assert((closing.primaryDuty < stopped.primaryDuty) ==
               (direction == hardware::Direction::Clockwise));
        for (const uint32_t pulse : {hardware::kReverseUs, hardware::kNeutralUs,
                                     hardware::kForwardUs}) {
            const uint32_t reconstructed = static_cast<uint32_t>(
                static_cast<uint64_t>(motor::pulseToDuty(pulse)) * 1000000UL /
                (static_cast<uint64_t>(1UL << hardware::kPwmResolutionBits) *
                 hardware::kPwmFrequencyHz));
            assert(reconstructed <= pulse + 1 && reconstructed + 1 >= pulse);
        }
#endif
    }
    const Limits between{false, false};
    const Limits opened{true, false};
    const Limits closed{false, true};
    const Limits fault{true, true};

    for (const char *text : {"open", "on", "close", "off", "OPEN", "", "openx"}) {
        const Command expected = strcmp(text, "open") == 0 || strcmp(text, "on") == 0
            ? Command::Open : strcmp(text, "close") == 0 || strcmp(text, "off") == 0
            ? Command::Close : Command::Invalid;
        assert(valve::parseCommand(reinterpret_cast<const uint8_t *>(text), strlen(text)) == expected);
    }
    const uint8_t embeddedNull[] = {'o', 'n', 0};
    assert(valve::parseCommand(embeddedNull, sizeof(embeddedNull)) == Command::Invalid);

    for (const Limits limits : {between, opened, closed, fault}) {
        for (const Command command : {Command::Open, Command::Close}) {
            Control control;
            assert(control.status(limits) == (limits.open && limits.closed ? Status::Error
                : limits.open ? Status::Open : limits.closed ? Status::Closed : Status::Unknown));
            control.request(command, limits, 1000);
            const bool blocked = command == Command::Open ? limits.open : limits.closed;
            assert(control.motion() == (blocked ? Motion::Stopped
                : command == Command::Open ? Motion::Opening : Motion::Closing));
            if (!blocked) {
                assert(control.status(between) == (command == Command::Open ? Status::Opening : Status::Closing));
                control.update(command == Command::Open ? opened : closed, 1001);
                assert(control.motion() == Motion::Stopped);
            }
        }
    }

    for (const Command command : {Command::Open, Command::Close}) {
        for (const Limits limits : {between, opened, closed, fault}) {
            Control running;
            running.request(command, between, 1000);
            const Status expected = limits.open && limits.closed ? Status::Error
                : limits.open ? Status::Open : limits.closed ? Status::Closed
                : command == Command::Open ? Status::Opening : Status::Closing;
            assert(running.status(limits) == expected);
            running.request(Command::Invalid, between, 1001);
            running.update(limits, 1002);
            const bool stopped = command == Command::Open ? limits.open : limits.closed;
            assert((running.motion() == Motion::Stopped) == stopped);
        }
    }
    assert(strcmp(valve::statusName(Status::Open), "open") == 0);
    assert(strcmp(valve::statusName(Status::Closed), "closed") == 0);
    assert(strcmp(valve::statusName(Status::Opening), "opening") == 0);
    assert(strcmp(valve::statusName(Status::Closing), "closing") == 0);
    assert(strcmp(valve::statusName(Status::Error), "error") == 0);
    assert(strcmp(valve::statusName(Status::Unknown), "unknown") == 0);

    Control control;
    control.request(Command::Open, closed, 1000);
    control.update(fault, 1001);
    assert(control.motion() == Motion::Stopped);
    assert(control.status(fault) == Status::Error);
    control.update(between, 2000);
    assert(control.motion() == Motion::Stopped);

    control.request(Command::Open, between, 2000);
    control.request(Command::Close, between, 2010);
    assert(control.motion() == Motion::Stopped);
    assert(control.status(between) == Status::Unknown);
    control.update(between, 2109);
    assert(control.motion() == Motion::Stopped);
    control.request(Command::Close, between, 2109);
    assert(control.motion() == Motion::Stopped);
    control.update(between, 2110);
    assert(control.motion() == Motion::Closing);
    control.request(Command::Close, between, 2200);
    control.update(between, 122110);
    assert(control.motion() == Motion::Stopped);
    assert(control.status(between) == Status::Unknown);

    Control pending;
    pending.request(Command::Open, between, 0);
    pending.update(opened, 50);
    pending.update(between, 200);
    assert(pending.motion() == Motion::Stopped);

    Control rollover;
    rollover.request(Command::Open, between, UINT32_MAX - 50);
    rollover.request(Command::Close, between, UINT32_MAX - 10);
    rollover.update(between, 88);
    assert(rollover.motion() == Motion::Stopped);
    rollover.update(between, 89);
    assert(rollover.motion() == Motion::Closing);

    Control independent[4];
    independent[0].request(Command::Open, between, 1000);
    independent[1].request(Command::Close, between, 1000);
    independent[0].update(opened, 1100);
    assert(independent[0].motion() == Motion::Stopped);
    assert(independent[1].motion() == Motion::Closing);
    assert(independent[2].motion() == Motion::Stopped);
    assert(independent[3].motion() == Motion::Stopped);
    puts("Valve control tests passed.");
}