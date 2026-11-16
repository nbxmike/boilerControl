#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "router.h"

struct fake_io {
	bool energized[8];
	unsigned int writes;
	unsigned int order[2];
	unsigned int settles;
	unsigned int fail_write;
};

static int write_relay(unsigned int relay, bool energized, void *context)
{
	struct fake_io *fake = context;

	fake->order[fake->writes++] = relay;
	if (fake->writes == fake->fail_write) {
		return -EIO;
	}
	fake->energized[relay] = energized;
	/* Neither side of a zone may join forced 24 VAC to the thermostat. */
	assert(!fake->energized[relay & ~1u] || fake->energized[relay | 1u]);
	return 0;
}

static void settle(unsigned int milliseconds, void *context)
{
	struct fake_io *fake = context;

	assert(milliseconds >= 20);
	assert(fake->writes == 1);
	++fake->settles;
}

static void test_transitions(void)
{
	struct router initial = {0};

	for (unsigned int zone = 0; zone < ROUTER_ZONE_COUNT; ++zone) {
		assert(initial.zones[zone] == ZONE_BYPASS);
		for (enum zone_mode from = ZONE_BYPASS; from < ZONE_INVALID; ++from) {
			for (enum zone_mode to = ZONE_BYPASS; to < ZONE_INVALID; ++to) {
				struct router router = {0};
				struct fake_io fake = {0};
				struct router_io io = {write_relay, settle, &fake};

				router.zones[zone] = from;
				fake.energized[zone * 2] = from == ZONE_ON;
				fake.energized[zone * 2 + 1] = from != ZONE_BYPASS;
				assert(router_apply(&router, zone, to, &io) == 0);
				assert(router.zones[zone] == to);
				assert(fake.energized[zone * 2] == (to == ZONE_ON));
				assert(fake.energized[zone * 2 + 1] == (to != ZONE_BYPASS));
				if (from == to) {
					assert(fake.writes == 0 && fake.settles == 0);
				} else {
					assert(fake.writes == 2 && fake.settles == 1);
					assert(fake.order[0] == zone * 2 + (to == ZONE_ON));
					assert(fake.order[1] == zone * 2 + (to != ZONE_ON));
				}
				for (unsigned int other = 0; other < ROUTER_ZONE_COUNT; ++other) {
					if (other != zone) {
						assert(router.zones[other] == ZONE_BYPASS);
						assert(!fake.energized[other * 2]);
						assert(!fake.energized[other * 2 + 1]);
					}
				}
			}
		}
	}
}

static void test_errors(void)
{
	for (enum zone_mode mode = ZONE_ON; mode <= ZONE_OFF; ++mode) {
		for (unsigned int failure = 1; failure <= 2; ++failure) {
			struct router router = {0};
			struct fake_io fake = {.fail_write = failure};
			struct router_io io = {write_relay, settle, &fake};

			assert(router_apply(&router, 0, mode, &io) == -EIO);
			assert(router.zones[0] == ZONE_BYPASS);
			assert(fake.writes == failure);
		}
	}
	struct router router = {0};
	struct fake_io fake = {0};
	struct router_io io = {write_relay, settle, &fake};

	assert(router_apply(&router, 4, ZONE_ON, &io) == -EINVAL);
	assert(router_apply(&router, 0, ZONE_INVALID, &io) == -EINVAL);
	assert(fake.writes == 0);
}

static void test_commands(void)
{
	for (unsigned int zone = 0; zone < ROUTER_ZONE_COUNT; ++zone) {
		const char *topic = router_command_topics[zone];
		char expected[64];

		snprintf(expected, sizeof(expected), "thermostat-router/zone%u/set", zone + 1);
		assert(strcmp(topic, expected) == 0);
		assert(router_command_zone((const uint8_t *)topic, strlen(topic)) == (int)zone);
		assert(router_command_zone((const uint8_t *)topic, strlen(topic) - 1) == -1);
		snprintf(expected, sizeof(expected), "thermostat-router/zone%u/state", zone + 1);
		assert(strcmp(router_state_topics[zone], expected) == 0);
	}
	const char *bad_topics[] = {
		"thermostat-router/zone0/set", "thermostat-router/zone5/set",
		"thermostat-router/zone1/state", "thermostat-router/zone1/set/extra",
		"other/zone1/set", "",
	};
	for (unsigned int i = 0; i < sizeof(bad_topics) / sizeof(bad_topics[0]); ++i) {
		assert(router_command_zone((const uint8_t *)bad_topics[i],
					   strlen(bad_topics[i])) == -1);
	}
	const char *commands[] = {"bypass", "on", "off"};
	for (unsigned int i = 0; i < 3; ++i) {
		assert(router_parse_mode((const uint8_t *)commands[i], strlen(commands[i])) ==
		       (enum zone_mode)i);
	}
	const char *bad_commands[] = {"", "ON", " on", "off\n", "bypassx", "o"};
	for (unsigned int i = 0; i < sizeof(bad_commands) / sizeof(bad_commands[0]); ++i) {
		assert(router_parse_mode((const uint8_t *)bad_commands[i],
					 strlen(bad_commands[i])) == ZONE_INVALID);
	}
	assert(router_parse_mode((const uint8_t *)"on\0off", 6) == ZONE_INVALID);
}

static void test_timing(void)
{
	assert(router_led_duration(false, false, true) == 2000);
	assert(router_led_duration(false, false, false) == 500);
	assert(router_led_duration(true, false, true) == 200);
	assert(router_led_duration(true, false, false) == 200);
	assert(router_led_duration(true, true, true) == 500);
	assert(router_led_duration(true, true, false) == 500);
	assert(router_led_duration(false, true, true) == 2000);

	struct report_schedule reports;
	int64_t start = 1234;

	router_report_start(&reports, start);
	for (unsigned int i = 0; i < 8; ++i) {
		int64_t due = start + (int64_t)(i + 1) * 120000;

		assert(reports.zone == i % 4);
		assert(!router_report_due(&reports, due - 1));
		assert(router_report_due(&reports, due));
		/* A failed publish does not advance the zone or deadline. */
		assert(router_report_due(&reports, due + 10));
		assert(reports.zone == i % 4);
		router_report_advance(&reports, due);
	}
	/* Reconnection after an outage sends one overdue report, not a burst. */
	assert(router_report_due(&reports, start + 2000000));
	router_report_advance(&reports, start + 2000000);
	assert(!router_report_due(&reports, start + 2000000));
	assert(router_report_due(&reports, start + 2120000));
}

int main(void)
{
	test_commands();
	test_transitions();
	test_errors();
	test_timing();
	puts("Routing, error handling, command validation, LED timing and reporting tests passed");
	return 0;
}
