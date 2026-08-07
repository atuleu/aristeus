#pragma once

#include "sl_status.h"

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif //__cplusplus

sl_status_t batt_monitor_init();

battery_level_t batt_monitor_get_current_level();

sl_status_t batt_monitor_start_measurement();

#ifdef __cplusplus
}
#endif //__cplusplus
