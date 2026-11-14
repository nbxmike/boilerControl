#pragma once

#include "motor_output.h"

namespace hardware {

/*
 * Check required GPIO/PWM devices and initialize all valve outputs and limit inputs.
 * Args: None.
 * Returns: Zero on success or a negative errno value on failure.
 */
int initialize();

/*
 * Continuously supervise valve commands, limits, and motor outputs.
 * Args:
 * p1 - unused Zephyr thread-entry parameter.
 * p2 - unused Zephyr thread-entry parameter.
 * p3 - unused Zephyr thread-entry parameter.
 * Returns: None; the supervisor thread runs indefinitely.
 */
void motorThread(void *, void *, void *);

/*
 * Submit the latest command for one valve to the motor supervisor.
 * Args:
 * index - zero-based valve index.
 * command - requested valve command.
 * Returns: None.
 */
void submit(unsigned index, valve::Command command);

/*
 * Read the most recently sampled status for one valve.
 * Args: index - zero-based valve index.
 * Returns: The latest valve status.
 */
valve::Status status(unsigned index);

}
