
#include "app.h"
#include "drivers/i2c_schd.h"
#include <stdint.h>

#include <sl_core.h>
#include <sl_device_gpio.h>
#include <sl_gpio.h>
#include <sl_i2c.h>
#include <sl_sleeptimer.h>
#include <sl_status.h>

#include <app_log.h>

#include <types.h>

#include <drivers/lps22hh.h>

sl_status_t lps22hh_read_blocking(
    lps22hh_handle_t *self, uint8_t start_reg, uint8_t count, uint8_t *buffer
) {
	uint8_t reg_address_buffer = start_reg;
	return i2c_schd_transfer_blocking(
	    self->i2c_bus,
	    self->address,
	    &reg_address_buffer,
	    1,
	    buffer,
	    count
	);
}

sl_status_t lps22hh_write_blocking(
    lps22hh_handle_t *self, uint8_t count, const uint8_t *buffer
) {
	if (count < 2) {
		return SL_STATUS_INVALID_COUNT;
	}

	return i2c_schd_send_blocking(self->i2c_bus, self->address, buffer, count);
}

void _lps22hh_oneshot_complete(
    lps22hh_handle_t *self, sl_status_t status, pressure_t pressure
) {
	lps22hh_readout_callback_t cb;
	void                      *user_data;

	CORE_ATOMIC_SECTION({
		cb                      = self->oneshot_callback;
		user_data               = (void *)self->oneshot_user_data;
		self->oneshot_callback  = NULL;
		self->oneshot_user_data = NULL;
	});

	if (cb != NULL) {
		cb(status, pressure, user_data);
	}
}

#define LPS22HH_ONESHOT_MAXTRIALS 10

void _lps22hh_oneshot_read_cb(i2c_tx_status_t status, void *user_data) {
	lps22hh_handle_t *self = user_data;
	CORE_DECLARE_IRQ_STATE;
	CORE_ENTER_ATOMIC();
	self->oneshot_reading = false;

	if (status != I2C_TX_OK) {

		if (self->oneshot_tries < LPS22HH_ONESHOT_MAXTRIALS) {
			app_proceed();
		} else {
			_lps22hh_oneshot_complete(
			    self,
			    i2c_tx_status_map(status),
			    0xffffffff
			);
		}
		CORE_EXIT_ATOMIC();
		return;
	}
	CORE_EXIT_ATOMIC();

	// checks if the device cleared the data ready as we read the the H
	// register.
	bool data_ready;
	sl_gpio_get_pin_input(self->data_ready_pin, &data_ready);
	if (data_ready == true) {
		app_log_warning(
		    "LPS22HH %s.0x%x has stale data, re-reading" APP_LOG_NL,
		    i2c_schd_get_instance_name(self->i2c_bus),
		    self->address
		);
		app_proceed();
		return;
	}
	int32_t pressure_data = ((uint32_t)self->read_buffer[2] << 16) |
	                        ((uint32_t)self->read_buffer[1] << 8) |
	                        ((uint32_t)self->read_buffer[0]);
	// data is 24bit signed in 2 complement, we do not support negative pressure
	if ((pressure_data & 0x00800000) != 0x00) {
		_lps22hh_oneshot_complete(self, SL_STATUS_INVALID_RANGE, 0xffffffff);
		return;
	}

	// data sensitivity is 4096 LSB/ hPA, pressure is in dPa
	float pressure_dPa = ((float)pressure_data) / 4.096f;
	_lps22hh_oneshot_complete(self, SL_STATUS_OK, (pressure_t)pressure_dPa);
}

void _lps22hh_oneshot_write_cb(i2c_tx_status_t status, void *user_data) {
	if (status == I2C_TX_OK) {
		// nothing todo, waiting INT.
		return;
	}
	// we could not write the command, terminate the async call
	lps22hh_handle_t *self = user_data;
	_lps22hh_oneshot_complete(self, i2c_tx_status_map(status), 0xffffffff);
}

sl_status_t lps22hh_oneshot(
    lps22hh_handle_t *self, lps22hh_readout_callback_t cb, void *user_data
) {
	if (cb == NULL) {
		return SL_STATUS_NULL_POINTER;
	}

	CORE_DECLARE_IRQ_STATE;
	CORE_ENTER_ATOMIC();
	if (self->oneshot_callback != NULL || self->oneshot_user_data != NULL) {
		CORE_EXIT_ATOMIC();
		return SL_STATUS_BUSY;
	}

	self->oneshot_callback  = cb;
	self->oneshot_user_data = user_data;
	self->oneshot_reading   = false;
	self->oneshot_tries     = 0;
	CORE_EXIT_ATOMIC();

	static const uint8_t oneshot_command[2] = {0x11, 0x11};

	sl_status_t s = i2c_schd_send(
	    self->i2c_bus,
	    self->address,
	    oneshot_command,
	    2,
	    &_lps22hh_oneshot_write_cb,
	    self
	);

	if (s != SL_STATUS_OK) {
		CORE_ENTER_ATOMIC();
		self->oneshot_callback  = NULL;
		self->oneshot_user_data = NULL;
		CORE_EXIT_ATOMIC();
	}

	return s;
}

sl_status_t lps22hh_init(lps22hh_handle_t *self, lps22hh_config_t *config) {
	if (self == NULL || config == NULL || config->i2c_bus == NULL ||
	    config->interrupt_pin == NULL) {
		return SL_STATUS_NULL_POINTER;
	}

	self->i2c_bus        = config->i2c_bus;
	self->address        = config->addrLSBSet ? 0x5d : 0x5c;
	self->data_ready_pin = config->interrupt_pin;

	self->oneshot_callback  = NULL;
	self->oneshot_user_data = NULL;
	self->oneshot_reading   = true;

	uint8_t     whoAmI;
	sl_status_t sc = lps22hh_read_blocking(self, 0x0f, 1, &whoAmI);
	if (sc != SL_STATUS_OK) {
		app_log_warning(
		    "No LPS22HH devices at %s.0x%x found, retrying in 80ms" APP_LOG_NL,
		    i2c_schd_get_instance_name(self->i2c_bus),
		    self->address
		);

		sl_sleeptimer_delay_millisecond(80);
		sc = lps22hh_read_blocking(self, 0x0f, 1, &whoAmI);

		if (sc != SL_STATUS_OK) {
			app_log_error(
			    "No LPS22HH devices at %s.0x%x found" APP_LOG_NL,
			    i2c_schd_get_instance_name(self->i2c_bus),
			    self->address
			);
			return SL_STATUS_INITIALIZATION;
		}
	}

	if (whoAmI != 0xb3) {
		app_log_error(
		    "LPS2DF device found at %s.0x%x, but wrong whoAmI value 0x%x "
		    "(expected 0xb3)" APP_LOG_NL,
		    i2c_schd_get_instance_name(self->i2c_bus),
		    self->address,
		    whoAmI
		);
		return SL_STATUS_INITIALIZATION;
	}

	app_log_info(
	    "found LPS22HH devices at %s.0x%x" APP_LOG_NL,
	    i2c_schd_get_instance_name(self->i2c_bus),
	    self->address
	);

	uint8_t config_buffer[4] = {
	    0x11, // CTRL_REG2, for first set, then CTRL_REG1 (0x10)  in second
	          // pass.
	    0x10, // reg 0x11, no command IF_ADD_INC_SET
	    0x10, // reg 0x11, no command IF_ADD_INC_SET (second pass)
	    0x04, // reg 0x12: DRDY set for interrupt.
	};

	// first make sure IF_ADD_INC_SET is set, so we can read/write multiple
	// registers in one TX.
	sc = lps22hh_write_blocking(self, 2, config_buffer);
	if (sc != SL_STATUS_OK) {
		app_log_error(
		    "LPS22HH %s.0x%x: could not set IF_ADD_INC" APP_LOG_NL,
		    i2c_schd_get_instance_name(self->i2c_bus),
		    self->address
		);
		return SL_STATUS_INITIALIZATION;
	}
	config_buffer[0] = 0x10; // CTRL_REG1 start;
	config_buffer[1] = 0x00; // One-shot mode, no low-pass no BDU no SIM.

	// rewrites all control register
	sc = lps22hh_write_blocking(self, sizeof(config_buffer), config_buffer);
	if (sc != SL_STATUS_OK) {
		app_log_error(
		    "LPS22HH %s.0x%x: could not set config" APP_LOG_NL,
		    i2c_schd_get_instance_name(self->i2c_bus),
		    self->address
		);
		return SL_STATUS_INITIALIZATION;
	}

	sc = sl_gpio_set_pin_mode(
	    self->data_ready_pin,
	    SL_GPIO_MODE_INPUT_PULL,
	    false
	);
	if (sc != SL_STATUS_OK) {
		app_log_error(
		    "LPS22HH %s.0x%x: could not set  pin mode" APP_LOG_NL,
		    i2c_schd_get_instance_name(self->i2c_bus),
		    self->address
		);
		return SL_STATUS_INITIALIZATION;
	}

	bool dummy;
	sc = sl_gpio_get_pin_input(self->data_ready_pin, &dummy);
	if (sc != SL_STATUS_OK) {
		app_log_error(
		    "LPS22HH %s.0x%x: could not read pin" APP_LOG_NL,
		    i2c_schd_get_instance_name(self->i2c_bus),
		    self->address
		);
		return SL_STATUS_INITIALIZATION;
	}

	return SL_STATUS_OK;
}

void lps22hh_process_action(lps22hh_handle_t *self) {
	CORE_DECLARE_IRQ_STATE;
	CORE_ENTER_ATOMIC();
	if (self->oneshot_callback == NULL || self->oneshot_reading == true) {
		CORE_EXIT_ATOMIC();
		return;
	}
	CORE_EXIT_ATOMIC();

	bool data_ready;
	sl_gpio_get_pin_input(self->data_ready_pin, &data_ready);
	if (data_ready == false) {
		// we mark that there is sill work to be done.
		app_proceed();
		return;
	}

	CORE_ENTER_ATOMIC();
	static uint8_t start_address[1] = {0x28};
	sl_status_t    status           = i2c_schd_transfer(
        self->i2c_bus,
        self->address,
        start_address,
        1,
        self->read_buffer,
        3,
        &_lps22hh_oneshot_read_cb,
        self
    );
	self->oneshot_tries += 1;
	if (status != SL_STATUS_OK) {
		// we retry on next iteration
		if (self->oneshot_tries < LPS22HH_ONESHOT_MAXTRIALS) {
			app_proceed();
		} else {
			_lps22hh_oneshot_complete(self, SL_STATUS_BUS_ERROR, 0xffffffff);
			return;
		}
	} else {
		self->oneshot_reading = true;
	}
	CORE_EXIT_ATOMIC();
	return;
}
