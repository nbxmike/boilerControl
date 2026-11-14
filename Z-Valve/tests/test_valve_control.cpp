#include <assert.h>
#include <initializer_list>
#include <stdio.h>

#include "command_topic.h"
#include "status_display.h"
#include "motor_output.h"

using valve::Command;
using valve::Control;
using valve::Limits;
using valve::Motion;
using valve::Status;

int main() {
    const Limits between{false, false};
    const Limits opened{true, false};
    const Limits closed{false, true};
    const Limits conflict{true, true};

    for (const Command command : {Command::Open, Command::Close}) {
        for (const Limits limits : {between, opened, closed, conflict}) {
            Control control;
            control.request(command, limits, 1000);
            const bool blocked = command == Command::Open ? limits.open : limits.closed;
            assert(control.motion() == (blocked ? Motion::Stopped :
                command == Command::Open ? Motion::Opening : Motion::Closing));
            if (!blocked) {
                const Limits destination = command == Command::Open ? opened : closed;
                control.update(destination, 1001);
                assert(control.motion() == Motion::Stopped);
                control.update(between, 2000);
                assert(control.motion() == Motion::Stopped);
            }
        }
    }

    Control reversing;
    reversing.request(Command::Open, between, 1000);
    reversing.request(Command::Close, between, 1010);
    assert(reversing.motion() == Motion::Stopped);
    assert(reversing.status(between) == Status::Unknown);
    reversing.request(Command::Close, between, 1109);
    assert(reversing.motion() == Motion::Stopped);
    reversing.update(between, 1110);
    assert(reversing.motion() == Motion::Closing);
    reversing.request(Command::Close, between, 1200);
    reversing.update(between, 121109);
    assert(reversing.motion() == Motion::Closing);
    reversing.update(between, 121110);
    assert(reversing.motion() == Motion::Stopped);
    reversing.update(between, 200000);
    assert(reversing.motion() == Motion::Stopped);

    Control conflictDuringReversal;
    conflictDuringReversal.request(Command::Open, between, 1000);
    conflictDuringReversal.request(Command::Close, between, 1010);
    conflictDuringReversal.update(conflict, 1050);
    assert(conflictDuringReversal.status(conflict) == Status::Error);
    conflictDuringReversal.update(between, 1200);
    assert(conflictDuringReversal.motion() == Motion::Stopped);

    Control rollover;
    rollover.request(Command::Open, between, UINT32_MAX - 50);
    rollover.request(Command::Close, between, UINT32_MAX - 10);
    rollover.update(between, 88);
    assert(rollover.motion() == Motion::Stopped);
    rollover.update(between, 89);
    assert(rollover.motion() == Motion::Closing);
    rollover.update(between, 120089);
    assert(rollover.motion() == Motion::Stopped);

    Control independent[4];
    independent[0].request(Command::Open, between, 1000);
    independent[1].request(Command::Close, between, 1000);
    independent[0].update(opened, 1010);
    assert(independent[0].motion() == Motion::Stopped);
    assert(independent[1].motion() == Motion::Closing);
    assert(independent[2].motion() == Motion::Stopped);
    assert(independent[3].motion() == Motion::Stopped);
    assert(independent[1].status(opened) == Status::Open);
    assert(independent[1].status(closed) == Status::Closed);
    assert(independent[1].status(between) == Status::Closing);
    assert(strcmp(valve::statusName(Status::Error), "error") == 0);
    assert(strcmp(valve::statusName(Status::Unknown), "unknown") == 0);

    const uint8_t displayIp[] = {192, 168, 10, 255};
    const valve::Status displayMotors[] = {
        Status::Open, Status::Closed, Status::Error, Status::Closing};
    char displayLines[status_display::kLineCount][status_display::kLineBufferSize]{};
    status_display::formatLines(true, true, displayIp, displayMotors, displayLines);
    assert(strcmp(displayLines[0], "192.168.10.255") == 0);
    assert(strcmp(displayLines[1], "MQTT Up") == 0);
    assert(strcmp(displayLines[2], "M1:Opn M2:Clsd") == 0);
    assert(strcmp(displayLines[3], "M3:ERR M4:Mov") == 0);
    status_display::formatLines(false, true, displayIp, displayMotors, displayLines);
    assert(strcmp(displayLines[0], "Down") == 0);
    assert(strcmp(displayLines[1], "MQTT Down") == 0);

    for (const char *payload : {"open", "on", "close", "off", "OPEN", "on\n", "", "openx"}) {
        const Command expected = strcmp(payload, "open") == 0 || strcmp(payload, "on") == 0
            ? Command::Open : strcmp(payload, "close") == 0 || strcmp(payload, "off") == 0
            ? Command::Close : Command::Invalid;
        assert(valve::parseCommand(reinterpret_cast<const uint8_t *>(payload),
                                   strlen(payload)) == expected);
    }
    const uint8_t embeddedNull[] = {'o', 'n', 0};
    assert(valve::parseCommand(embeddedNull, sizeof(embeddedNull)) == Command::Invalid);
    const uint8_t notTerminated[] = {'o', 'p', 'e', 'n'};
    assert(valve::parseCommand(notTerminated, sizeof(notTerminated)) == Command::Open);
    // The wildcard subscription also receives our own retained availability message.
    for (const char *topic : {"heater-valves/availability", "heater-valves/valve1/state",
                              "heater-valves/valve0", "heater-valves/valve5",
                              "heater-valves/valve10", "heater-valves/valve1/setx",
                              "other/valve1", "heater-valves/valve1/SET", ""}) {
        assert(valve::commandValve(reinterpret_cast<const uint8_t *>(topic),
                                   strlen(topic), "heater-valves") == -1);
    }
    for (unsigned index = 0; index < 4; ++index) {
        char topic[64];
        snprintf(topic, sizeof(topic), "heater-valves/valve%u", index + 1);
        assert(valve::commandValve(reinterpret_cast<const uint8_t *>(topic),
                                   strlen(topic), "heater-valves") == static_cast<int>(index));
        strcat(topic, "/set");
        assert(valve::commandValve(reinterpret_cast<const uint8_t *>(topic),
                                   strlen(topic), "heater-valves") == static_cast<int>(index));
    }

    for (const bool clockwise : {false, true}) {
        const motor::Output stopped = motor::outputFor(Motion::Stopped, clockwise);
        const motor::Output opening = motor::outputFor(Motion::Opening, clockwise);
        const motor::Output closing = motor::outputFor(Motion::Closing, clockwise);
        assert(!stopped.enabled && opening.enabled && closing.enabled);
        if constexpr (motor::kBts7960) {
            assert(stopped.primaryNs == 0 && stopped.secondaryNs == 0);
            assert(opening.primaryNs == closing.secondaryNs);
            assert(opening.secondaryNs == closing.primaryNs);
            assert(opening.primaryNs == (clockwise ? motor::kMotorPulseNs : 0));
        } else {
            assert(stopped.primaryNs == motor::kNeutralUs * 1000);
            const uint32_t forward = (motor::kNeutralUs +
                (motor::kForwardUs - motor::kNeutralUs) * motor::kSpeedPermille / 1000) * 1000;
            const uint32_t reverse = (motor::kNeutralUs -
                (motor::kNeutralUs - motor::kReverseUs) * motor::kSpeedPermille / 1000) * 1000;
            assert(opening.primaryNs == (clockwise ? forward : reverse));
            assert(closing.primaryNs == (clockwise ? reverse : forward));
            assert(opening.secondaryNs == 0 && closing.secondaryNs == 0);
            assert(opening.primaryNs < motor::kPeriodNs && closing.primaryNs < motor::kPeriodNs);
            // Matching integer-Hz frames keeps duty-to-pulse error below one nanosecond.
            const uint64_t reconstructed = static_cast<uint64_t>(forward) * 1000000000ULL /
                (static_cast<uint64_t>(motor::kPeriodNs) * motor::kPwmFrequencyHz);
            assert(reconstructed >= forward && reconstructed <= forward + 1);
        }
    }
    printf("Valve behavior tests passed (%s).\n", motor::kControllerName);
}
