#ifndef THERMOSTAT_APP_H
#define THERMOSTAT_APP_H

#include "router.h"

int app_set_zone(unsigned int zone, enum zone_mode mode);
enum zone_mode app_zone_mode(unsigned int zone);
void app_network_status(bool *wifi, bool *mqtt);
void app_run_network(void);

#endif
