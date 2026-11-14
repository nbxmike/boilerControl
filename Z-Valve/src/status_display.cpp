#include <string.h>

#include <zephyr/device.h>
#include <zephyr/display/cfb.h>
#include <zephyr/drivers/display.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "network.h"
#include "status_display.h"
#include "valve_hardware.h"

LOG_MODULE_REGISTER(status_display, LOG_LEVEL_INF);

namespace {

constexpr int64_t kRefreshIntervalMs = 60000;
constexpr int64_t kRetryIntervalMs = 5000;
constexpr uint8_t kMaxStartLine = 32;
constexpr int16_t kRowHeight = 16;
constexpr uint8_t kI2cCommandControl = 0x00;
constexpr uint8_t kSetStartLine = 0x40;

const i2c_dt_spec sh1106I2c = I2C_DT_SPEC_GET(DT_NODELABEL(sh1106));

/*
 * Compare network fields that affect the displayed IP and readiness labels.
 * Args: left/right - cached and current network snapshots.
 * Returns: True when the rendered network state would be identical.
 */
bool sameNetworkStatus(const network::DisplaySnapshot &left,
                       const network::DisplaySnapshot &right) {
    return left.wifiUp == right.wifiUp && left.mqttUp == right.mqttUp &&
        (!left.wifiUp || memcmp(left.ipv4, right.ipv4, sizeof(left.ipv4)) == 0);
}

/*
 * Compare every valve state shown on the display.
 * Args: left/right - four cached and current valve states.
 * Returns: True when all four states match.
 */
bool sameMotorStatus(const valve::Status left[motor::kValveCount],
                     const valve::Status right[motor::kValveCount]) {
    for (unsigned index = 0; index < motor::kValveCount; ++index) {
        if (left[index] != right[index]) return false;
    }
    return true;
}

/*
 * Render four compact rows and circularly offset the SH1106 scan start by one pixel.
 * Args: display - ready SH1106 device; snapshot - network status; motors - four valve states;
 * startLine - vertical burn-in offset in the range 0-32.
 * Returns: Zero on success or the first framebuffer/I2C error.
 */
int render(const device *display, const network::DisplaySnapshot &snapshot,
           const valve::Status motors[motor::kValveCount], uint8_t startLine) {
    char lines[status_display::kLineCount][status_display::kLineBufferSize]{};
    status_display::formatLines(snapshot.wifiUp, snapshot.mqttUp, snapshot.ipv4,
                                motors, lines);

    int result = cfb_framebuffer_clear(display, false);
    if (result < 0) return result;
    for (unsigned row = 0; row < status_display::kLineCount; ++row) {
        result = cfb_draw_text(display, lines[row], 0,
                               static_cast<int16_t>(row * kRowHeight));
        if (result < 0) return result;
    }
    result = cfb_framebuffer_finalize(display);
    if (result < 0) return result;

    /* SH1106 start-line addressing wraps the whole frame at the panel edge without clipping rows. */
    const uint8_t command[] = {
        kI2cCommandControl,
        static_cast<uint8_t>(kSetStartLine | (startLine & 0x3f)),
    };
    return i2c_write_dt(&sh1106I2c, command, sizeof(command));
}

}

namespace status_display {

/*
 * Initialize the display and refresh it when observed state changes or its timer expires.
 * Args: Zephyr's three required thread-entry arguments are unused; state is read from subsystem APIs.
 * Returns: None; initialization failures are logged and terminate this thread.
 */
void thread(void *, void *, void *) {
    const device *display = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
    if (!device_is_ready(display)) {
        LOG_ERR("SH1106 display is not ready");
        return;
    }

    int result = display_set_pixel_format(display, PIXEL_FORMAT_MONO10);
    if (result < 0) result = display_set_pixel_format(display, PIXEL_FORMAT_MONO01);
    if (result < 0) {
        LOG_ERR("SH1106 pixel format setup failed: %d", result);
        return;
    }
    result = cfb_framebuffer_init(display);
    if (result < 0) {
        LOG_ERR("SH1106 framebuffer initialization failed: %d", result);
        return;
    }
    uint8_t fontWidth = 0;
    uint8_t fontHeight = 0;
    if (cfb_get_numof_fonts(display) < 1 ||
        cfb_get_font_size(display, 0, &fontWidth, &fontHeight) < 0 ||
        fontWidth != 10 || fontHeight != kRowHeight) {
        LOG_ERR("SH1106 requires the built-in 10x16 character font");
        cfb_framebuffer_deinit(display);
        return;
    }
    result = cfb_framebuffer_set_font(display, 0);
    if (result == 0) result = cfb_set_kerning(display, -2);
    if (result == 0) result = display_blanking_off(display);
    if (result < 0) {
        LOG_ERR("SH1106 display setup failed: %d", result);
        cfb_framebuffer_deinit(display);
        return;
    }

    network::DisplaySnapshot previousNetwork{};
    valve::Status previousMotors[motor::kValveCount]{};
    bool havePrevious = false;
    uint8_t startLine = 0;
    int64_t nextRefresh = 0;
    int64_t retryAt = 0;
    for (;;) {
        const network::DisplaySnapshot network = network::displaySnapshot();
        valve::Status motors[motor::kValveCount];
        for (unsigned index = 0; index < motor::kValveCount; ++index) {
            motors[index] = hardware::status(index);
        }

        const bool changed = !havePrevious ||
            !sameNetworkStatus(network, previousNetwork) ||
            !sameMotorStatus(motors, previousMotors);
        const int64_t now = k_uptime_get();
        if ((changed || now >= nextRefresh) && now >= retryAt) {
            result = render(display, network, motors, startLine);
            if (result < 0) {
                LOG_ERR("SH1106 refresh failed: %d", result);
                retryAt = now + kRetryIntervalMs;
            } else {
                previousNetwork = network;
                memcpy(previousMotors, motors, sizeof(previousMotors));
                havePrevious = true;
                nextRefresh = now + kRefreshIntervalMs;
                retryAt = 0;
                startLine = startLine == kMaxStartLine ? 0 : startLine + 1;
            }
        }
        k_sleep(K_MSEC(250));
    }
}

}
