#pragma once

#include "drivers/i2c_schd.h"
#include "sl_sleeptimer.h"
#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

/**
 * Callback for asynchronous environment sensing readout. This callback is
 * called when the readout is complete, either successfully or with an error.
 */

typedef void (*app_es_readout_callback_t)(
    sl_status_t status, const data_point_t *dp
);

/**
 * Configuration structure for the environment sensing application. This
 * structure contains the necessary parameters to initialize the application,
 * including the I2C bus handle, data ready pin configuration, readout period,
 * and the callback function to be called upon readout completion.
 */
typedef struct {
	i2c_schd_handle_t        *i2c_bus;
	const sl_gpio_t           data_ready_pin;
	uint32_t                  readout_period_ms;
	app_es_readout_callback_t callback;
} app_es_config_t;

/**
 * Initialize the environment sensing application with the provided
 * configuration.
 *
 * @return SL_STATUS_OK if the initialization was successful, or an error code
 *         otherwise.
 */
sl_status_t app_es_init(const app_es_config_t *config);

/**
 * Returns the current data point. This function should not be called from an
 * ISR context.
 */
const data_point_t *app_es_current_data();

/**
 * Sets the current Unix time to mark readout. This function can be called from
 * an ISR context.
 */

void app_es_set_current_unix_time(sl_sleeptimer_timestamp_t now);

/**
 * Returns the current Unix time, adjusted by the time offset. This function can
 * be called from an ISR context.
 */
sl_sleeptimer_timestamp_t app_es_get_current_unix_time();

/**
 * Sleep preemption check for app_is_ok_to_sleep(). This function must be called
 * from a context where ISR are disabled, like in app_is_ok_to_sleep().
 */
bool app_es_is_ok_to_sleep();

/**
 * Process any pending environment sensing operations. This function should be
 * called periodically from the main loop to ensure that environment sensing
 * operations are completed in a timely manner. It has its own gate mechanism,
 * so do not gate it behind app_proceed().
 */
void app_es_process_action();

/**
 * returns current pressure.
 *
 */
pressure_t app_es_current_pressure();

/**
 * Tares the pressure to the provided value. Will save it in NVM
 */
sl_status_t app_es_tare_pressure(pressure_t pressure);

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

sl_status_t _app_es_start_co2_readout();
void        _app_es_schedule_batt_measurement();

void _app_es_process_sht4x(sl_status_t status);
void _app_es_process_lps22hh(sl_status_t status);
void _app_es_process_stcc4(sl_status_t status);
void _app_es_complete_readout(sl_status_t status);

sl_status_t _app_es_start_readout_timer();

void _app_es_on_conditioning_done(sl_status_t status, void *user_data);
void _app_es_on_factory_reset(sl_status_t status, void *user_data);
void _app_es_on_stcc4_start_continuous(sl_status_t status, void *user_data);
void _app_es_on_stcc4_stop_continuous(sl_status_t status, void *user_data);
void _app_es_on_stcc4_init_timeout(
    sl_sleeptimer_timer_handle_t *timer, void *user_data
);

#ifdef __cplusplus
}
#endif // __cplusplus
