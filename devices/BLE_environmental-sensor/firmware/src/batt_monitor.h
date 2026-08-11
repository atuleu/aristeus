#pragma once

#include "sl_status.h"

#include "types.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif //__cplusplus

sl_status_t batt_monitor_init();

battery_level_t batt_monitor_get_current_level();

sl_status_t batt_monitor_start_open_measurement(uint32_t delay_ticks);

sl_status_t batt_monitor_start_loaded_measurement(uint32_t delay_ticks);

uint16_t batt_monitor_open_voltage_mV();
uint16_t batt_monitor_loaded_voltage_mV();

#ifdef __cplusplus
}
#endif //__cplusplus
