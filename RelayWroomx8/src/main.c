#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/reboot.h>

#include "app.h"

LOG_MODULE_REGISTER(thermostat_router, LOG_LEVEL_INF);

#define USER_NODE DT_PATH(zephyr_user)
#define RELAY_SPEC(node, prop, index) GPIO_DT_SPEC_GET_BY_IDX(node, prop, index),

static const struct gpio_dt_spec relays[] = {
	DT_FOREACH_PROP_ELEM(USER_NODE, relay_gpios, RELAY_SPEC)
};
static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(USER_NODE, status_led_gpios);
static struct router router;
static struct k_thread led_thread;
K_THREAD_STACK_DEFINE(led_stack, 1024);

BUILD_ASSERT(ARRAY_SIZE(relays) == ROUTER_ZONE_COUNT * 2);

static int write_relay(unsigned int relay, bool energized, void *context)
{
	ARG_UNUSED(context);
	return gpio_pin_set_dt(&relays[relay], energized);
}

static void settle_relays(unsigned int milliseconds, void *context)
{
	ARG_UNUSED(context);
	k_sleep(K_MSEC(milliseconds));
}

int app_set_zone(unsigned int zone, enum zone_mode mode)
{
	const struct router_io io = {
		.write = write_relay,
		.settle = settle_relays,
	};
	int result = router_apply(&router, zone, mode, &io);

	if (result < 0) {
		LOG_ERR("Zone %u GPIO transition failed: %d; resetting to bypass", zone + 1,
			result);
		for (unsigned int parity = 0; parity < 2; ++parity) {
			for (unsigned int i = parity; i < ARRAY_SIZE(relays); i += 2) {
				int reset = gpio_pin_set_dt(&relays[i], 0);

				if (reset < 0) {
					LOG_ERR("Relay %u could not be deenergized: %d", i + 1,
						reset);
				}
			}
			k_sleep(K_MSEC(ROUTER_RELAY_SETTLE_MS));
		}
		sys_reboot(SYS_REBOOT_COLD);
	}
	return result;
}

enum zone_mode app_zone_mode(unsigned int zone)
{
	return router.zones[zone];
}

static void blink_led(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);
	bool illuminated = true;

	for (;;) {
		bool wifi;
		bool mqtt;
		int result = gpio_pin_set_dt(&led, illuminated);

		if (result < 0) {
			LOG_ERR("Status LED write failed: %d", result);
			return;
		}
		app_network_status(&wifi, &mqtt);
		k_sleep(K_MSEC(router_led_duration(wifi, mqtt, illuminated)));
		illuminated = !illuminated;
	}
}

int main(void)
{
	/* Configure every relay inactive before starting any network operation. */
	for (unsigned int i = 0; i < ARRAY_SIZE(relays); ++i) {
		if (!gpio_is_ready_dt(&relays[i])) {
			LOG_ERR("Relay %u GPIO controller is not ready", i + 1);
			return -ENODEV;
		}
		int result = gpio_pin_configure_dt(&relays[i], GPIO_OUTPUT_INACTIVE);

		if (result < 0) {
			LOG_ERR("Relay %u initialization failed: %d", i + 1, result);
			return result;
		}
	}
	if (!gpio_is_ready_dt(&led)) {
		LOG_ERR("LED GPIO controller is not ready");
		return -ENODEV;
	}
	int result = gpio_pin_configure_dt(&led, GPIO_OUTPUT_INACTIVE);

	if (result < 0) {
		LOG_ERR("LED initialization failed: %d", result);
		return result;
	}
	k_thread_create(&led_thread, led_stack, K_THREAD_STACK_SIZEOF(led_stack),
			blink_led, NULL, NULL, NULL, 7, 0, K_NO_WAIT);
	LOG_INF("All four zones initialized in bypass");
	app_run_network();
	return 0;
}
