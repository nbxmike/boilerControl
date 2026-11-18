#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "network.h"
#include "status_display.h"
#include "valve_hardware.h"

LOG_MODULE_REGISTER(valve_main, LOG_LEVEL_INF);

namespace {
K_THREAD_STACK_DEFINE(motorStack, 3072);
K_THREAD_STACK_DEFINE(displayStack, 3072);
k_thread motorThreadData;
k_thread displayThreadData;
}

/*
 * Initialize valve hardware, start the high-priority motor supervisor, then manage networking.
 * If safe hardware initialization fails, return before any network command can be accepted.
 */
int main() {
    const int result = hardware::initialize();
    if (result < 0) {
        LOG_ERR("Valve initialization failed (%d); networking and motion disabled", result);
        return result;
    }

    /* Keep motor and limit supervision on its own priority-2 thread so network work cannot stall it. */
    k_tid_t tid = k_thread_create(&motorThreadData, motorStack,
        K_THREAD_STACK_SIZEOF(motorStack), hardware::motorThread,
        nullptr, nullptr, nullptr, K_PRIO_PREEMPT(2), 0, K_NO_WAIT);
    k_thread_name_set(tid, "valve-motors");

    /* Keep display I2C transfers below both motor supervision and network work. */
    tid = k_thread_create(&displayThreadData, displayStack,
        K_THREAD_STACK_SIZEOF(displayStack), status_display::thread,
        nullptr, nullptr, nullptr, K_PRIO_PREEMPT(8), 0, K_NO_WAIT);
    k_thread_name_set(tid, "status-display");

    /* The network manager owns the main thread; it never returns during normal operation. */
    runNetwork();
    return 0;
}
