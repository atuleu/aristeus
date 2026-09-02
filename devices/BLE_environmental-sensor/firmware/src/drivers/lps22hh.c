
#include "app.h"
#include "app_config.h"
#include "drivers/i2c_schd.h"
#include "utils/status.h"
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

void _lps22hh_start_poll_timer(lps22hh_handle_t *self);

void _lps22hh_on_timer_timeout(
    sl_sleeptimer_timer_handle_t *timer, void *user_data
);

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
#define LPS22HH_DATA_PIN_POLL_PERIOD_MS 1

void _lps22hh_oneshot_read_cb(i2c_tx_status_t status, void *user_data) {
	lps22hh_handle_t *self = user_data;

	if (status != I2C_TX_OK) {
		if (self->oneshot_tries >= LPS22HH_ONESHOT_MAXTRIALS) {
			app_log_error("[LPS22HH] too many read attempt failure." APP_LOG_NL
			);
			_lps22hh_oneshot_complete(
			    self,
			    i2c_tx_status_map(status),
			    GATT_PRESSURE_NAN
			);
			return;
		}
		// re-schedule another poll.
		_lps22hh_start_poll_timer(self);
		return;
	}

	// checks if the device cleared the data ready as we read the the H
	// register.
	bool data_ready;
	sl_gpio_get_pin_input(&self->data_ready_pin, &data_ready);
	if (data_ready == true) {
		app_log_warning(
		    "[LPS22HH %s.0x%x] has stale data, re-reading" APP_LOG_NL,
		    i2c_schd_get_instance_name(self->i2c_bus),
		    self->address
		);
		_lps22hh_start_poll_timer(self);
		return;
	}
	int32_t pressure_data = ((uint32_t)self->read_buffer[2] << 16) |
	                        ((uint32_t)self->read_buffer[1] << 8) |
	                        ((uint32_t)self->read_buffer[0]);
	// data is 24bit signed in 2 complement, we do not support negative pressure
	if ((pressure_data & 0x00800000) != 0x00) {
		_lps22hh_oneshot_complete(
		    self,
		    SL_STATUS_INVALID_RANGE,
		    GATT_PRESSURE_NAN
		);
		return;
	}

	// data sensitivity is 4096 LSB/ hPA, pressure is in dPa
	float pressure_dPa = ((float)pressure_data) / 4.096f;
	_lps22hh_oneshot_complete(self, SL_STATUS_OK, (pressure_t)pressure_dPa);
}

void _lps22hh_oneshot_write_cb(i2c_tx_status_t status, void *user_data) {
	lps22hh_handle_t *self = user_data;
	if (status == I2C_TX_OK) {
		self->data_ready_timeout_ms = -LPS22HH_DATA_PIN_POLL_PERIOD_MS;
		_lps22hh_start_poll_timer(self);
		return;
	}
	// we could not write the command, terminate the async call

	_lps22hh_oneshot_complete(
	    self,
	    i2c_tx_status_map(status),
	    GATT_PRESSURE_NAN
	);
}

sl_status_t lps22hh_oneshot(
    lps22hh_handle_t *self, lps22hh_readout_callback_t cb, void *user_data
) {
	app_log_debug("[LPS22HH] starting one-shot." APP_LOG_NL);
	if (cb == NULL) {
		return SL_STATUS_NULL_POINTER;
	}

	CORE_DECLARE_IRQ_STATE;
	CORE_ENTER_ATOMIC();
	if (self->oneshot_callback != NULL) {
		CORE_EXIT_ATOMIC();
		return SL_STATUS_BUSY;
	}

	self->oneshot_callback  = cb;
	self->oneshot_user_data = user_data;
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
	if (self == NULL || config == NULL || config->i2c_bus == NULL) {
		return SL_STATUS_NULL_POINTER;
	}

	self->i2c_bus        = config->i2c_bus;
	self->address        = config->addrLSBSet ? 0x5d : 0x5c;
	self->data_ready_pin = config->interrupt_pin;

	self->oneshot_callback  = NULL;
	self->oneshot_user_data = NULL;

	uint8_t whoAmI;

	sl_status_t status;
	for (uint8_t i = 0; i < (DEVICE_INIT_CONNECT_MAX_TRIES - 1); ++i) {
		status = lps22hh_read_blocking(self, 0x0f, 1, &whoAmI);
		if (status == SL_STATUS_OK) {
			break;
		}
		app_log_warning(
		    "[LPS22HH] no device at %s.0x%x found, retrying in "
		    "%dms, reason: %s" APP_LOG_NL,
		    i2c_schd_get_instance_name(self->i2c_bus),
		    self->address,
		    DEVICE_CONNECT_RETRIES_TIMEOUT_MS,
		    sl_status_get_string(status)
		);

		sl_sleeptimer_delay_millisecond(DEVICE_CONNECT_RETRIES_TIMEOUT_MS);
	}

	if (status != SL_STATUS_OK) {
		status = lps22hh_read_blocking(self, 0x0f, 1, &whoAmI);
	}

	if (status != SL_STATUS_OK) {
		app_log_error(
		    "[LPS22HH] no device at %s.0x%x found" APP_LOG_NL,
		    i2c_schd_get_instance_name(self->i2c_bus),
		    self->address
		);
		return SL_STATUS_INITIALIZATION;
	}

	if (whoAmI != 0xb3) {
		app_log_error(
		    "[LPS2HH] device found at %s.0x%x, but wrong whoAmI value 0x%x "
		    "(expected 0xb3)" APP_LOG_NL,
		    i2c_schd_get_instance_name(self->i2c_bus),
		    self->address,
		    whoAmI
		);
		return SL_STATUS_INITIALIZATION;
	}

	app_log_debug(
	    "[LPS22HH] found device at %s.0x%x" APP_LOG_NL,
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
	status = lps22hh_write_blocking(self, 2, config_buffer);
	if (status != SL_STATUS_OK) {
		app_log_error(
		    "[LPS22HH %s.0x%x] could not set IF_ADD_INC" APP_LOG_NL,
		    i2c_schd_get_instance_name(self->i2c_bus),
		    self->address
		);
		return SL_STATUS_INITIALIZATION;
	}
	config_buffer[0] = 0x10; // CTRL_REG1 start;
	config_buffer[1] = 0x00; // One-shot mode, no low-pass no BDU no SIM.

	// rewrites all control register
	status = lps22hh_write_blocking(self, sizeof(config_buffer), config_buffer);
	if (status != SL_STATUS_OK) {
		app_log_error(
		    "[LPS22HH %s.0x%x] could not set config" APP_LOG_NL,
		    i2c_schd_get_instance_name(self->i2c_bus),
		    self->address
		);
		return SL_STATUS_INITIALIZATION;
	}

	status = sl_gpio_set_pin_mode(
	    &self->data_ready_pin,
	    SL_GPIO_MODE_INPUT_PULL,
	    false
	);
	if (status != SL_STATUS_OK) {
		app_log_error(
		    "[LPS22HH %s.0x%x] could not set  pin mode" APP_LOG_NL,
		    i2c_schd_get_instance_name(self->i2c_bus),
		    self->address
		);
		return SL_STATUS_INITIALIZATION;
	}

	bool dummy;
	status = sl_gpio_get_pin_input(&self->data_ready_pin, &dummy);
	if (status != SL_STATUS_OK) {
		app_log_error(
		    "[LPS22HH %s.0x%x] could not read pin" APP_LOG_NL,
		    i2c_schd_get_instance_name(self->i2c_bus),
		    self->address
		);
		return SL_STATUS_INITIALIZATION;
	}

	return SL_STATUS_OK;
}

void _lps22hh_start_poll_timer(lps22hh_handle_t *self) {
	self->data_ready_timeout_ms += LPS22HH_DATA_PIN_POLL_PERIOD_MS;
	if (self->data_ready_timeout_ms >= 500) {
		_lps22hh_oneshot_complete(self, SL_STATUS_TIMEOUT, GATT_PRESSURE_NAN);
		return;
	}
	sl_status_t status = sl_sleeptimer_start_timer_ms(
	    &self->timer,
	    LPS22HH_DATA_PIN_POLL_PERIOD_MS,
	    &_lps22hh_on_timer_timeout,
	    self,
	    0,
	    0
	);
	if (status != SL_STATUS_OK) {
		_lps22hh_oneshot_complete(self, status, GATT_PRESSURE_NAN);
	}
}

void _lps22hh_on_timer_timeout(
    sl_sleeptimer_timer_handle_t *timer, void *user_data
) {
	(void)timer;
	lps22hh_handle_t *self = user_data;
	CORE_DECLARE_IRQ_STATE;
	CORE_ENTER_ATOMIC();
	if (self->oneshot_callback == NULL) {
		CORE_EXIT_ATOMIC();
		app_log_error(
		    "[LPS22HH] spurious timer firing after read completion." APP_LOG_NL
		);
		return;
	}
	CORE_EXIT_ATOMIC();

	bool data_ready;
	sl_gpio_get_pin_input(&self->data_ready_pin, &data_ready);
	if (data_ready == false) {
		_lps22hh_start_poll_timer(self);
		return;
	}

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
	if (status == SL_STATUS_OK) {
		// we are reading, we are waiting
		return;
	}

	if (self->oneshot_tries < LPS22HH_ONESHOT_MAXTRIALS) {
		app_log_debug("[LPS22HH] re-scheduling another read later." APP_LOG_NL);
		// reschedule another trial
		_lps22hh_start_poll_timer(self);
		return;
	}
	app_log_error(
	    "[LPS22HH] too many read scheduling attempt failed, "
	    "failing completion." APP_LOG_NL
	);

	_lps22hh_oneshot_complete(self, SL_STATUS_BUS_ERROR, GATT_PRESSURE_NAN);
}
