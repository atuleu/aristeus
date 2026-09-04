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

#define STCC4_NO_RESULT 0xFFFF

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
typedef void (*stcc4_operation_callback_t)(
    sl_status_t status, uint16_t result, void *user_data
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
    stcc4_handle_t            *self,
    temperature_t              temperature,
    humidity_t                 humidity,
    pressure_t                 pressure,
    stcc4_operation_callback_t callback,
    void                      *user_data,
    bool                       sleep_after_op
);

sl_status_t stcc4_enter_sleep_mode(stcc4_handle_t *self);

/**
 * Perform a factory reset of the chip, including the conditionnning.
 */
sl_status_t stcc4_factory_reset(
    stcc4_handle_t *self,
    stcc4_operation_callback_t,
    void *user_data,
    bool  sleep_after_op
);

/**
 * Performs a conditionning of the chip.
 */
sl_status_t stcc4_perform_conditioning(
    stcc4_handle_t            *self,
    stcc4_operation_callback_t callback,
    void                      *user_data,
    bool                       sleep_after_op
);

/**
 * Performs an FRC calibratrion
 */
sl_status_t stcc4_perform_FRC_calibration(
    stcc4_handle_t            *self,
    co2_concentration_t        co2,
    stcc4_operation_callback_t callback,
    void                      *user_data,
    bool                       sleep_after_op
);

/**
 * Performs an FRC calibratrion
 */
sl_status_t stcc4_perform_self_test(
    stcc4_handle_t            *self,
    stcc4_operation_callback_t callback,
    void                      *user_data,
    bool                       sleep_after_op
);

/**
 * Perform a factory reset of the chip, including the conditionnning.
 */
sl_status_t stcc4_soft_reset(
    stcc4_handle_t *self,
    stcc4_operation_callback_t,
    void *user_data,
    bool  sleep_after_op
);

/**
 * Structure for the STCC4 driver handle given for static initialization
 * only. This structure is considered opaque and should not be accessed
 * directly.
 */
struct stcc4_handle {
	i2c_schd_handle_t *i2c_bus;
	uint8_t            address;
	volatile bool      sleeping;

	uint8_t                    buffer[12];
	volatile i2c_tx_callback_t tx_callback;
	volatile void             *tx_user_data;
	uint16_t                   read_delay_ms;
	uint8_t                    read_len;

	volatile stcc4_operation_callback_t op_callback;
	volatile void                      *user_data;
	volatile bool                       sleep_after_op;
	temperature_t                       temperature;
	humidity_t                          humidity;
	pressure_t                          pressure;
	co2_concentration_t                 frc_pressure;
	co2_concentration_t                 pending_result;
	sl_status_t                         pending_status;
	sl_sleeptimer_timer_handle_t        timer;
};

// private TX callbacks and functions
typedef uint16_t _stcc4_command_t;
sl_status_t      _stcc4_send_command_blocking(
         stcc4_handle_t  *self,
         _stcc4_command_t cmd,
         uint16_t         read_delay_ms,
         uint8_t         *read_buffer,
         uint8_t          read_len
     );
sl_status_t _stcc4_send_command(
    stcc4_handle_t   *self,
    _stcc4_command_t  cmd,
    uint8_t           command_len,
    uint16_t          read_delay_ms,
    uint8_t           read_len,
    i2c_tx_callback_t callback,
    void             *user_data
);
sl_status_t _stcc4_read_serial_number_blocking(
    stcc4_handle_t *self, uint32_t *serial_number
);

void _stcc4_on_command_write(i2c_tx_status_t status, void *user_data);
void _stcc4_tx_timer_timeout(
    sl_sleeptimer_timer_handle_t *timer, void *user_data
);
void _stcc4_complete_tx(i2c_tx_status_t status, void *user_data);

bool _stcc4_check_crc_word(const uint8_t *buffer);
void _stcc4_write_word(uint8_t *buffer, uint16_t word);

// operation completion
void _stcc4_complete_pending_operation(stcc4_handle_t *self);
void _stcc4_complete_operation(
    stcc4_handle_t *self, sl_status_t status, uint16_t co2
);
void _stcc4_schedule_operation_completion(
    stcc4_handle_t *self, sl_status_t status, uint16_t co2
);

// private sleep mode
sl_status_t
_stcc4_send_exit_sleep_mode(stcc4_handle_t *self, i2c_tx_callback_t on_wakeup);

sl_status_t
     _stcc4_enter_sleep_mode(stcc4_handle_t *self, CORE_irqState_t irqState);
void _stcc4_on_enter_sleepmode(i2c_tx_status_t status, void *user_data);

// readout sequence
void _stcc4_start_readout_sequence(i2c_tx_status_t status, void *user_data);
void _stcc4_on_set_rht_compensation(i2c_tx_status_t status, void *user_data);
void _stcc4_on_set_pressure_compensation(
    i2c_tx_status_t status, void *user_data
);
void _stcc4_on_measure_single_shot(i2c_tx_status_t status, void *user_data);
void _stcc4_on_read_measurement(i2c_tx_status_t status, void *user_data);

// perform_conditionning
void _stcc4_start_conditioning(i2c_tx_status_t status, void *user_data);
void _stcc4_on_no_result_operation(i2c_tx_status_t status, void *user_data);
// factory reset
void _stcc4_start_factory_reset(i2c_tx_status_t status, void *user_data);
void _stcc4_on_factory_reset(i2c_tx_status_t status, void *user_data);

// FRC calibration
void _stcc4_start_FRC(i2c_tx_status_t status, void *user_data);
void _stcc4_on_word_result_operation(i2c_tx_status_t status, void *user_data);

// FRC calibration
void _stcc4_start_self_test(i2c_tx_status_t status, void *user_data);

// soft
void _stcc4_start_soft_reset(i2c_tx_status_t status, void *user_data);

#ifdef __cplusplus
}
#endif
