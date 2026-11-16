#include <errno.h>
#include <string.h>

#include "router.h"

const char *const router_command_topics[ROUTER_ZONE_COUNT] = {
	"thermostat-router/zone1/set", "thermostat-router/zone2/set",
	"thermostat-router/zone3/set", "thermostat-router/zone4/set",
};

const char *const router_state_topics[ROUTER_ZONE_COUNT] = {
	"thermostat-router/zone1/state", "thermostat-router/zone2/state",
	"thermostat-router/zone3/state", "thermostat-router/zone4/state",
};

int router_command_zone(const uint8_t *topic, size_t length)
{
	for (unsigned int i = 0; i < ROUTER_ZONE_COUNT; ++i) {
		if (length == strlen(router_command_topics[i]) &&
		    memcmp(topic, router_command_topics[i], length) == 0) {
			return (int)i;
		}
	}
	return -1;
}

enum zone_mode router_parse_mode(const uint8_t *payload, size_t length)
{
	for (enum zone_mode mode = ZONE_BYPASS; mode < ZONE_INVALID; ++mode) {
		const char *name = router_mode_name(mode);

		if (length == strlen(name) && memcmp(payload, name, length) == 0) {
			return mode;
		}
	}
	return ZONE_INVALID;
}

const char *router_mode_name(enum zone_mode mode)
{
	switch (mode) {
	case ZONE_BYPASS: return "bypass";
	case ZONE_ON: return "on";
	case ZONE_OFF: return "off";
	default: return "invalid";
	}
}

int router_apply(struct router *router, unsigned int zone, enum zone_mode mode,
		 const struct router_io *io)
{
	if (zone >= ROUTER_ZONE_COUNT || mode < ZONE_BYPASS || mode >= ZONE_INVALID) {
		return -EINVAL;
	}
	if (router->zones[zone] == mode) {
		return 0;
	}

	unsigned int odd = zone * 2;
	unsigned int even = odd + 1;
	int result;

	/* Disconnect the thermostat before applying 24 VAC; remove 24 VAC
	 * before reconnecting the thermostat. Allow mechanical contacts to settle.
	 */
	if (mode == ZONE_ON) {
		result = io->write(even, true, io->context);
		if (result < 0) {
			return result;
		}
		io->settle(ROUTER_RELAY_SETTLE_MS, io->context);
		result = io->write(odd, true, io->context);
	} else {
		result = io->write(odd, false, io->context);
		if (result < 0) {
			return result;
		}
		io->settle(ROUTER_RELAY_SETTLE_MS, io->context);
		result = io->write(even, mode == ZONE_OFF, io->context);
	}
	if (result == 0) {
		router->zones[zone] = mode;
	}
	return result;
}

unsigned int router_led_duration(bool wifi, bool mqtt, bool illuminated)
{
	if (!wifi) {
		return illuminated ? 2000 : 500;
	}
	return mqtt ? 500 : 200;
}

void router_report_start(struct report_schedule *schedule, int64_t now)
{
	schedule->zone = 0;
	schedule->deadline = now + ROUTER_REPORT_INTERVAL_MS;
}

bool router_report_due(const struct report_schedule *schedule, int64_t now)
{
	return now >= schedule->deadline;
}

void router_report_advance(struct report_schedule *schedule, int64_t now)
{
	schedule->zone = (schedule->zone + 1) % ROUTER_ZONE_COUNT;
	schedule->deadline = now + ROUTER_REPORT_INTERVAL_MS;
}
