#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>

#include "valve_hardware.h"

LOG_MODULE_REGISTER(valve_hardware, LOG_LEVEL_INF);

namespace {

struct ValveIo {
    gpio_dt_spec openLimit;
    gpio_dt_spec closedLimit;
    gpio_dt_spec enable;
    pwm_dt_spec primary;
    pwm_dt_spec secondary;
    bool openClockwise;
};

/*
 * Bind each valve to the selected driver's Devicetree signals.
 * Compile-time checks below reject mismatched PWM counts and enable wiring.
 */
#if MOTOR_CONTROLLER == MOTOR_CONTROLLER_BTS7960
#define VALVE_IO(node) {GPIO_DT_SPEC_GET(node, open_limit_gpios), \
    GPIO_DT_SPEC_GET(node, closed_limit_gpios), GPIO_DT_SPEC_GET(node, enable_gpios), \
    PWM_DT_SPEC_GET_BY_IDX(node, 0), PWM_DT_SPEC_GET_BY_IDX(node, 1), \
    !DT_PROP(node, open_counterclockwise)}
#define CHECK_IO(node) BUILD_ASSERT(DT_PROP_LEN(node, pwms) == 2, "BTS7960 needs two PWM channels");
#else
#define VALVE_IO(node) {GPIO_DT_SPEC_GET(node, open_limit_gpios), \
    GPIO_DT_SPEC_GET(node, closed_limit_gpios), {}, PWM_DT_SPEC_GET_BY_IDX(node, 0), {}, \
    !DT_PROP(node, open_counterclockwise)}
#define CHECK_IO(node) BUILD_ASSERT(DT_PROP_LEN(node, pwms) == 1 && \
    !DT_NODE_HAS_PROP(node, enable_gpios), "RC PWM needs one signal and no enable");
#endif

DT_FOREACH_CHILD(DT_PATH(valves), CHECK_IO)
constexpr ValveIo valves[] = {
    VALVE_IO(DT_NODELABEL(valve1)), VALVE_IO(DT_NODELABEL(valve2)),
    VALVE_IO(DT_NODELABEL(valve3)), VALVE_IO(DT_NODELABEL(valve4)),
};
static_assert(ARRAY_SIZE(valves) == motor::kValveCount, "Four valves required");

atomic_t commands[motor::kValveCount];
atomic_t statuses[motor::kValveCount];

/*
 * Apply one channel's pulse width using the common controller frame period.
 * Args: pwm - configured PWM channel; pulseNs - requested high time in nanoseconds.
 * Returns: Zero on success or the PWM driver's negative errno.
 */
int setPwm(const pwm_dt_spec &pwm, uint32_t pulseNs) {
    return pwm_set(pwm.dev, pwm.channel, motor::kPeriodNs, pulseNs, pwm.flags);
}

/*
 * Stop a valve according to its driver: disable and clear a bridge, or command RC neutral.
 * Args: index - zero-based valve index.
 * Returns: The first negative GPIO/PWM error, or zero when every stop output succeeds.
 */
int stopValve(unsigned index) {
    const ValveIo &io = valves[index];
    if constexpr (motor::kBts7960) {
        /* Attempt every stop output even if disabling one of them fails. */
        const int enable = gpio_pin_set_dt(&io.enable, 0);
        const int primary = setPwm(io.primary, 0);
        const int secondary = setPwm(io.secondary, 0);
        return enable < 0 ? enable : primary < 0 ? primary : secondary;
    }
    return setPwm(io.primary, motor::kNeutralUs * 1000);
}

/*
 * Apply a new direction only after disabling/clearing both bridge legs; RC drivers use one pulse.
 * Args: index - zero-based valve index; motion - requested logical direction.
 * Returns: Zero on success or the first negative GPIO/PWM error.
 */
int apply(unsigned index, valve::Motion motion) {
    if (motion == valve::Motion::Stopped) return stopValve(index);
    const ValveIo &io = valves[index];
    const motor::Output output = motor::outputFor(motion, io.openClockwise);
    if constexpr (motor::kBts7960) {
        int result = gpio_pin_set_dt(&io.enable, 0);
        if (result < 0) return result;
        /* Clear both legs before applying a direction, with the bridge disabled. */
        result = setPwm(io.primary, 0);
        if (result < 0) return result;
        result = setPwm(io.secondary, 0);
        if (result < 0) return result;
        result = setPwm(io.primary, output.primaryNs);
        if (result < 0) return result;
        result = setPwm(io.secondary, output.secondaryNs);
        if (result < 0) return result;
        return gpio_pin_set_dt(&io.enable, 1);
    }
    return setPwm(io.primary, output.primaryNs);
}

/*
 * Best-effort stop all valves after a startup or system-level failure.
 * Args: None.
 * Returns: None; individual output failures are logged.
 */
void stopAll() {
    for (unsigned index = 0; index < motor::kValveCount; ++index) {
        const int result = stopValve(index);
        if (result < 0) LOG_ERR("Valve %u emergency stop failed: %d", index + 1, result);
    }
}

/*
 * Sample both active-low end stops and return their asserted states.
 * Args: index - zero-based valve index; limits - receives the sampled states.
 * Returns: Zero on success or the GPIO driver's negative errno.
 */
int readLimits(unsigned index, valve::Limits &limits) {
    const int open = gpio_pin_get_dt(&valves[index].openLimit);
    const int closed = gpio_pin_get_dt(&valves[index].closedLimit);
    if (open < 0 || closed < 0) return open < 0 ? open : closed;
    limits = {open != 0, closed != 0};
    return 0;
}

/*
 * Stop a valve after an I/O fault and latch its error status until reboot.
 * Args: index - zero-based valve index; error - negative I/O error that caused the fault.
 * Returns: None; stop failures are logged separately.
 */
void latchFault(unsigned index, int error) {
    LOG_ERR("Valve %u I/O fault (%d); motion latched off until reboot", index + 1, error);
    const int stopped = stopValve(index);
    if (stopped < 0) LOG_ERR("Valve %u stop output failed: %d", index + 1, stopped);
    atomic_set(&statuses[index], static_cast<atomic_val_t>(valve::Status::Error));
}

}

namespace hardware {

/*
 * Validate devices, hold BTS bridges disabled, stop all motors, and sample initial limits.
 * Returns failure before the supervisor or network can start if any required setup fails.
 */
int initialize() {
    /* Check every device before configuring outputs, so missing hardware cannot partially start. */
    for (const ValveIo &io : valves) {
        if (!gpio_is_ready_dt(&io.openLimit) || !gpio_is_ready_dt(&io.closedLimit) ||
            !pwm_is_ready_dt(&io.primary) ||
            (motor::kBts7960 && (!gpio_is_ready_dt(&io.enable) ||
                               !pwm_is_ready_dt(&io.secondary)))) {
            LOG_ERR("Required GPIO/PWM device is not ready");
            return -ENODEV;
        }
    }
    if constexpr (motor::kBts7960) {
        /* Drive all bridge enables inactive before touching PWM channels or limits. */
        int firstError = 0;
        for (const ValveIo &io : valves) {
            const int result = gpio_pin_configure_dt(&io.enable, GPIO_OUTPUT_INACTIVE);
            if (result < 0) {
                LOG_ERR("Bridge enable configuration failed: %d", result);
                if (firstError == 0) firstError = result;
            }
        }
        if (firstError < 0) return firstError;
    }
    /* Set safe stopped outputs first, then configure/sample limits for each valve. */
    for (unsigned index = 0; index < motor::kValveCount; ++index) {
        int result = apply(index, valve::Motion::Stopped);
        if (result == 0) result = gpio_pin_configure_dt(&valves[index].openLimit, GPIO_INPUT);
        if (result == 0) result = gpio_pin_configure_dt(&valves[index].closedLimit, GPIO_INPUT);
        valve::Limits limits{};
        if (result == 0) result = readLimits(index, limits);
        if (result < 0) {
            /* A partial initialization must not leave an earlier valve energized. */
            stopAll();
            return result;
        }
        atomic_set(&statuses[index],
                   static_cast<atomic_val_t>(valve::Control().status(limits)));
    }
    LOG_INF("Four valves initialized; motor controller %s", motor::kControllerName);
    return 0;
}

/*
 * Replace a valve's pending request in its atomic latest-command mailbox.
 * Args: index - zero-based valve index; command - newest command to process.
 * Returns: None.
 */
void submit(unsigned index, valve::Command command) {
    atomic_set(&commands[index], static_cast<atomic_val_t>(command));
}

/*
 * Read the latest atomic status snapshot published by the motor supervisor.
 * Args: index - zero-based valve index.
 * Returns: The latest status for that valve.
 */
valve::Status status(unsigned index) {
    return static_cast<valve::Status>(atomic_get(&statuses[index]));
}

/*
 * Zephyr requires three thread-entry arguments; this supervisor uses shared mailboxes instead.
 * Poll limits and commands independently for each valve so network delays cannot pause safety checks.
 */
void motorThread(void *, void *, void *) {
    valve::Control controls[motor::kValveCount] = {
        valve::Control(motor::kReversalDelayMs, motor::kMaxTravelMs),
        valve::Control(motor::kReversalDelayMs, motor::kMaxTravelMs),
        valve::Control(motor::kReversalDelayMs, motor::kMaxTravelMs),
        valve::Control(motor::kReversalDelayMs, motor::kMaxTravelMs),
    };
    valve::Motion applied[motor::kValveCount]{};
    bool faulted[motor::kValveCount]{};
    for (;;) {
        const uint32_t now = k_uptime_get_32();
        for (unsigned index = 0; index < motor::kValveCount; ++index) {
            /* Atomically consume the latest request and reset its mailbox to Invalid. */
            const auto command = static_cast<valve::Command>(
                atomic_set(&commands[index], static_cast<atomic_val_t>(valve::Command::Invalid)));
            if (faulted[index]) continue;
            valve::Limits limits{};
            int result = readLimits(index, limits);
            if (result == 0) {
                /* The state machine enforces limits, timeout, and reversal pause before outputs change. */
                controls[index].request(command, limits, now);
                const valve::Motion motion = controls[index].motion();
                if (motion != applied[index]) {
                    result = apply(index, motion);
                    if (result == 0) applied[index] = motion;
                }
            }
            if (result < 0) {
                /* Latch I/O failures off rather than repeatedly applying an uncertain motor command. */
                faulted[index] = true;
                latchFault(index, result);
            } else {
                atomic_set(&statuses[index],
                    static_cast<atomic_val_t>(controls[index].status(limits)));
            }
        }
        k_sleep(K_MSEC(1));
    }
}

}
