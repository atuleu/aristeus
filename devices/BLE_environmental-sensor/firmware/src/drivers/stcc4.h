#pragma once

#include "drivers/i2c_schd.h"
#include "sl_device_gpio.h"
#include "sl_sleeptimer.h"
#include "sl_status.h"
#include "types.h"
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Handle for the STCC4 driver. It is considered opaque and should not be
 * accessed directly.
 */
typedef struct stcc4_handle stcc4_handle_t;

/**
 * Initialization arguments for the STCC4 driver.
 */
typedef struct stcc4_init_args {
	i2c_schd_handle_t *i2c_bus;
	bool               address_pin_set;
} stcc4_init_args_t;

/**
 * Synchronously initialize the STCC4 driver. This function will block until the
 * initialization is complete.
 *
 * @param self Pointer to the STCC4 handle to initialize.
 * @param args Pointer to the initialization arguments.
 *
 * @return SL_STATUS_OK if the initialization was successful, or an error code
 *         otherwise.
 */
sl_status_t stcc4_init(stcc4_handle_t *self, stcc4_init_args_t *args);

/**
 * Callback for reading the CO2 concentration. This callback is called when the
 * read sequence is complete, either successfully or with an error.
 *
 * @param status The status of the read sequence.
 * @param concentration The CO2 concentration in ppm, or GATT_CO2_NAN if the
 *        read failed.
 * @param user_data The user data passed to the read sequence.
 *
 */
typedef void (*stcc4_readout_callback_t)(
    sl_status_t status, co2_concentration_t concentration, void *user_data
);

/**
 * Asynchronously start a read sequence on the STCC4 sensor. This function will
 * return immediately, and the readout callback will be called when the read
 * sequence is complete.
 *
 * @param self Pointer to the STCC4 handle.
 * @param temperature The current temperature in 0.01°C.
 * @param humidity The current relative humidity in 0.1%.
 * @param pressure The current atmospheric pressure in dPa.
 * @param callback The callback to call when the read sequence is complete.
 * @param user_data The user data to pass to the callback.
 *
 * @return SL_STATUS_OK if the read sequence was started successfully, or an
 *         error code otherwise.
 */
sl_status_t stcc4_start_read_sequence(
    stcc4_handle_t          *self,
    temperature_t            temperature,
    humidity_t               humidity,
    pressure_t               pressure,
    stcc4_readout_callback_t callback,
    void                    *user_data
);

sl_status_t stcc4_enter_sleep_mode(stcc4_handle_t *self);

/**
 *  Callback for asynchronous operation on the STCC4
 *
 */

typedef void (*stcc4_operation_callback_t)(sl_status_t status, void *user_data);

/**
 * Perform a factory reset of the chip, including the conditionnning.
 */
sl_status_t stcc4_factory_reset(
    stcc4_handle_t *self, stcc4_operation_callback_t, void *user_data
);

/**
 * Performs a conditionning of the chip.
 */
sl_status_t stcc4_perform_conditioning(
    stcc4_handle_t *self, stcc4_operation_callback_t callback, void *user_data
);

sl_status_t stcc4_start_continuous_measurement(
    stcc4_handle_t *self, stcc4_operation_callback_t, void *user_data
);

sl_status_t stcc4_stop_continous_measurement(
    stcc4_handle_t *self, stcc4_operation_callback_t, void *user_data
);

/**
 * Structure for the STCC4 driver handle given for static initialization
 * only. This structure is considered opaque and should not be accessed
 * directly.
 */
struct stcc4_handle {
	i2c_schd_handle_t *i2c_bus;
	uint8_t            address;
	bool               sleeping;

	uint8_t                    buffer[12];
	volatile i2c_tx_callback_t tx_callback;
	volatile void             *tx_user_data;
	uint16_t                   read_delay_ms;
	uint8_t                    read_len;

	volatile stcc4_operation_callback_t op_callback;
	volatile stcc4_readout_callback_t   read_callback;
	volatile void                      *user_data;
	temperature_t                       temperature;
	humidity_t                          humidity;
	pressure_t                          pressure;
	co2_concentration_t                 last_readout;
	sl_status_t                         last_status;
	sl_sleeptimer_timer_handle_t        timer;
};

#ifdef __cplusplus
}
#endif
