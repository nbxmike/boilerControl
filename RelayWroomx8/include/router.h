#ifndef THERMOSTAT_ROUTER_H
#define THERMOSTAT_ROUTER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define ROUTER_ZONE_COUNT 4
#define ROUTER_REPORT_INTERVAL_MS 120000
#define ROUTER_RELAY_SETTLE_MS 20

enum zone_mode {
	ZONE_BYPASS,
	ZONE_ON,
	ZONE_OFF,
	ZONE_INVALID
};

struct router {
	enum zone_mode zones[ROUTER_ZONE_COUNT];
};

struct router_io {
	int (*write)(unsigned int relay, bool energized, void *context);
	void (*settle)(unsigned int milliseconds, void *context);
	void *context;
};

struct report_schedule {
	unsigned int zone;
	int64_t deadline;
};

extern const char *const router_command_topics[ROUTER_ZONE_COUNT];
extern const char *const router_state_topics[ROUTER_ZONE_COUNT];

int router_command_zone(const uint8_t *topic, size_t length);
enum zone_mode router_parse_mode(const uint8_t *payload, size_t length);
const char *router_mode_name(enum zone_mode mode);
int router_apply(struct router *router, unsigned int zone, enum zone_mode mode,
		 const struct router_io *io);
unsigned int router_led_duration(bool wifi, bool mqtt, bool illuminated);
void router_report_start(struct report_schedule *schedule, int64_t now);
bool router_report_due(const struct report_schedule *schedule, int64_t now);
void router_report_advance(struct report_schedule *schedule, int64_t now);

#endif
