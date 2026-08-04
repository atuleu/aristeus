#pragma once

#include "sl_status.h"
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif //__cplusplus

sl_status_t batt_monitor_init();

typedef uint8_t battery_level_t;
#define BATTERY_NAN 0xff

battery_level_t batt_monitor_get_current_level();

sl_status_t batt_monitor_start_measurement();

#ifdef __cplusplus
}
#endif //__cplusplus
