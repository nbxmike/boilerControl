#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace valve {

// Commands are requests; motion is the current output state; status also reflects limits.
enum class Command : uint8_t { Invalid, Open, Close };
enum class Motion : uint8_t { Stopped, Opening, Closing };
enum class Status : uint8_t { Unknown, Open, Closed, Opening, Closing, Error };

struct Limits {
    bool open;
    bool closed;
};

// MQTT payloads are not necessarily null-terminated; accept only exact lowercase commands.
inline Command parseCommand(const uint8_t *payload, size_t length) {
    if ((length == 4 && memcmp(payload, "open", 4) == 0) ||
        (length == 2 && memcmp(payload, "on", 2) == 0)) {
        return Command::Open;
    }
    if ((length == 5 && memcmp(payload, "close", 5) == 0) ||
        (length == 3 && memcmp(payload, "off", 3) == 0)) {
        return Command::Close;
    }
    return Command::Invalid;
}

// Stable MQTT payload strings, independent of the selected motor-controller hardware.
inline const char *statusName(Status status) {
    switch (status) {
    case Status::Open: return "open";
    case Status::Closed: return "closed";
    case Status::Opening: return "opening";
    case Status::Closing: return "closing";
    case Status::Error: return "error";
    default: return "unknown";
    }
}

// Hardware-independent state machine, instantiated separately for each valve.
// Callers supply sampled limits and milliseconds; unsigned subtraction tolerates rollover.
class Control {
public:
    explicit Control(uint32_t reversalDelayMs = 100, uint32_t maxTravelMs = 120000)
        : reversalDelayMs_(reversalDelayMs), maxTravelMs_(maxTravelMs) {}

    // Apply current safety conditions before accepting a request or queuing a reversal.
    // Repeating a running command must not restart its maximum-travel timer.
    void request(Command command, Limits limits, uint32_t now) {
        update(limits, now);
        if (command == Command::Invalid) {
            return;
        }
        const Motion requested = command == Command::Open ? Motion::Opening : Motion::Closing;
        if (blocked(requested, limits)) {
            stop(now);
            return;
        }
        if (motion_ == requested) {
            return;
        }
        stop(now);
        pending_ = requested;
        update(limits, now);
    }

    // Limit conflicts, destination hits, and travel expiry cancel both active and pending motion.
    // A pending direction starts only after the required stopped interval has elapsed.
    void update(Limits limits, uint32_t now) {
        if ((limits.open && limits.closed) || blocked(motion_, limits) ||
            blocked(pending_, limits)) {
            stop(now);
            return;
        }
        if (motion_ != Motion::Stopped && now - startedAt_ >= maxTravelMs_) {
            stop(now);
            return;
        }
        if (pending_ != Motion::Stopped && now - stoppedAt_ >= reversalDelayMs_) {
            motion_ = pending_;
            pending_ = Motion::Stopped;
            startedAt_ = now;
        }
    }

    Motion motion() const { return motion_; }

    // Physical end stops take precedence over motion when reporting valve position.
    Status status(Limits limits) const {
        if (limits.open && limits.closed) return Status::Error;
        if (limits.open) return Status::Open;
        if (limits.closed) return Status::Closed;
        if (motion_ == Motion::Opening) return Status::Opening;
        if (motion_ == Motion::Closing) return Status::Closing;
        return Status::Unknown;
    }

private:
    // Only the destination limit blocks a direction, allowing travel away from an end stop.
    static bool blocked(Motion motion, Limits limits) {
        return (motion == Motion::Opening && limits.open) ||
               (motion == Motion::Closing && limits.closed);
    }

    // Preserve the original stop time while idle; clearing pending motion prevents auto-restart.
    void stop(uint32_t now) {
        if (motion_ != Motion::Stopped) {
            stoppedAt_ = now;
        }
        motion_ = Motion::Stopped;
        pending_ = Motion::Stopped;
    }

    Motion motion_ = Motion::Stopped;
    Motion pending_ = Motion::Stopped;
    uint32_t startedAt_ = 0;
    uint32_t stoppedAt_ = 0;
    const uint32_t reversalDelayMs_;
    const uint32_t maxTravelMs_;
};

}