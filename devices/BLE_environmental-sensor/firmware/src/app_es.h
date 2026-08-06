#pragma once

#include "drivers/i2c_schd.h"
#include "sl_sleeptimer.h"
#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

typedef void (*app_es_readout_callback_t)(
    sl_status_t status, const data_point_t *dp
);

typedef struct {
	i2c_schd_handle_t        *i2c_bus;
	const sl_gpio_t           data_ready_pin;
	uint32_t                  readout_period_ms;
	app_es_readout_callback_t callback;
} app_es_config_t;

sl_status_t app_es_init(const app_es_config_t *config);

const data_point_t *app_es_current_data();

void app_es_set_current_unix_time(sl_sleeptimer_timestamp_t now);

sl_sleeptimer_timestamp_t app_es_get_current_unix_time();

bool app_es_is_ok_to_sleep();
void app_es_process_action();

void _app_es_on_lps22hh_readout(
    sl_status_t status, pressure_t pressure, void *user_data
);
void _app_es_on_sht4x_readout(
    sl_status_t   status,
    temperature_t temperature,
    humidity_t    humidity,
    void         *user_data
);

void _app_es_on_stcc4_readout(
    sl_status_t status, co2_concentration_t co2, void *user_data
);
void _app_es_on_sensor_timer_timeout(
    sl_sleeptimer_timer_handle_t *timer, void *user_data
);

void _app_es_on_batt_timer_timeout(
    sl_sleeptimer_timer_handle_t *timer, void *user_data
);

sl_status_t _app_es_start_co2_readout();
void        _app_es_schedule_batt_measurement();

void _app_es_process_sht4x(sl_status_t status);
void _app_es_process_lps22hh(sl_status_t status);
void _app_es_process_stcc4(sl_status_t status);
void _app_es_complete_readout(sl_status_t status);
#ifdef __cplusplus
}
#endif // __cplusplus
