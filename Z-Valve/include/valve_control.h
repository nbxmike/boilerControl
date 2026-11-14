#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace valve {

enum class Command : uint8_t { Invalid, Open, Close };
enum class Motion : uint8_t { Stopped, Opening, Closing };
enum class Status : uint8_t { Unknown, Open, Closed, Opening, Closing, Error };

struct Limits {
    bool open;
    bool closed;
};

/*
 * Parse an exact lowercase open/close MQTT command payload.
 * Args:
 * payload - payload bytes.
 * length - payload size in bytes.
 * Returns: The parsed command, or Command::Invalid if it is unsupported.
 */
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

/*
 * Return the stable MQTT text corresponding to a valve status.
 * Args: status - valve status to convert.
 * Returns: A pointer to a static, null-terminated status string.
 */
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

// Port of the Arduino controller's independent, rollover-safe motion state machine.
class Control {
public:
    /*
     * Configure the minimum stopped interval for reversals and maximum travel time.
     * Args:
     * reversalDelayMs - minimum stopped time.
     * maxTravelMs - maximum continuous travel.
     * Returns: None.
     */
    explicit Control(uint32_t reversalDelayMs = 100, uint32_t maxTravelMs = 120000)
        : reversalDelayMs_(reversalDelayMs), maxTravelMs_(maxTravelMs) {}

    /*
     * Apply a new motion request after enforcing current limits and travel timeout.
     * Args:
     * command - requested direction.
     * limits - sampled end stops.
     * now - current uptime in ms.
     * Returns: None.
     */
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

    /*
     * Enforce end stops, conflicts, travel timeout, and any pending reversal.
     * Args:
     * limits - sampled end stops.
     * now - current uptime in ms.
     * Returns: None.
     */
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

    /*
     * Read the currently applied motion direction.
     * Args: None.
     * Returns: The current motion state.
     */
    Motion motion() const { return motion_; }

    /*
     * Determine valve status, giving physical end stops priority over motion.
     * Args: limits - sampled end stops.
     * Returns: The reported valve status.
     */
    Status status(Limits limits) const {
        if (limits.open && limits.closed) return Status::Error;
        if (limits.open) return Status::Open;
        if (limits.closed) return Status::Closed;
        if (motion_ == Motion::Opening) return Status::Opening;
        if (motion_ == Motion::Closing) return Status::Closing;
        return Status::Unknown;
    }

private:
    /*
     * Check whether a direction is blocked by its destination end stop.
     * Args:
     * motion - direction to check.
     * limits - sampled end stops.
     * Returns: True when the destination limit blocks the direction.
     */
    static bool blocked(Motion motion, Limits limits) {
        return (motion == Motion::Opening && limits.open) ||
               (motion == Motion::Closing && limits.closed);
    }

    /*
     * Stop active and pending motion while recording the stop time when needed.
     * Args: now - current uptime in ms.
     * Returns: None.
     */
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
