
#include <stdint.h>

#include <sl_core.h>
#include <sl_device_gpio.h>
#include <sl_gpio.h>
#include <sl_i2c.h>
#include <sl_sleeptimer.h>
#include <sl_status.h>

#include <app_log.h>

#include <types.h>

#include <drivers/i2c_utils.h>
#include <drivers/lps22df.h>

void app_lps22df_tx_complete(app_lps22df_handle_t *self, sl_status_t status) {
	app_lps22df_tx_callback_t cb;
	void                     *user_data;

	cb                 = self->tx_callback;
	user_data          = (void *)self->tx_user_data;
	self->tx_callback  = NULL;
	self->tx_user_data = NULL;

	i2c_unclaim_instance(self->i2c_bus);

	cb(status, user_data);
}

sl_status_t
app_lps22df_on_i2c_transfer_complete(sl_i2c_handle_t *i2c, void *user_data) {
	(void)i2c;

	app_lps22df_handle_t *self = user_data;

	app_lps22df_tx_complete(self, SL_STATUS_OK);
	return SL_STATUS_OK;
}

sl_status_t app_lps22df_on_i2c_event(
    sl_i2c_handle_t *i2c, sl_i2c_event_t e, void *user_data
) {
	(void)i2c;

	if (e == SL_I2C_EVENT_IN_PROGRESS || e == SL_I2C_EVENT_COMPLETED ||
	    e == SL_I2C_EVENT_IDLE) {
		return SL_STATUS_OK;
	}

	app_lps22df_handle_t *self = user_data;
	app_lps22df_tx_complete(
	    self,
	    e == SL_I2C_EVENT_DATA_NACK ? SL_STATUS_NOT_FOUND : SL_STATUS_BUS_ERROR
	);

	return SL_STATUS_OK;
}

sl_status_t app_lps22df_read(
    app_lps22df_handle_t     *self,
    uint8_t                   start_reg,
    uint8_t                   count,
    uint8_t                  *buffer,
    app_lps22df_tx_callback_t cb,
    void                     *user_data
) {

	sl_status_t sc = i2c_claim_instance(self->i2c_bus);

	if (sc != SL_STATUS_OK) {
		return sc;
	}

	sc = sl_i2c_set_transfer_complete_callback(
	    self->i2c_bus,
	    &app_lps22df_on_i2c_transfer_complete
	);

	if (sc != SL_STATUS_OK) {
		i2c_unclaim_instance(self->i2c_bus);
		return sc;
	}

	sc = sl_i2c_set_event_callback(self->i2c_bus, &app_lps22df_on_i2c_event);
	if (sc != SL_STATUS_OK) {
		i2c_unclaim_instance(self->i2c_bus);
		return sc;
	}

	self->tx_callback  = cb;
	self->tx_user_data = user_data;

	self->reg_address_buffer = start_reg;

	sc = sl_i2c_leader_transfer_non_blocking(
	    self->i2c_bus,
	    self->address,
	    &self->reg_address_buffer,
	    1,
	    buffer,
	    count,
	    (void *)self
	);

	if (sc != SL_STATUS_OK) {
		self->tx_callback  = NULL;
		self->tx_user_data = NULL;
		i2c_unclaim_instance(self->i2c_bus);

		return sc;
	}

	return sc;
}

sl_status_t app_lps22df_write(
    app_lps22df_handle_t     *self,
    uint8_t                   count,
    const uint8_t            *buffer,
    app_lps22df_tx_callback_t cb,
    void                     *user_data
) {
	if (count < 2) {
		return SL_STATUS_INVALID_COUNT;
	}

	sl_status_t sc = i2c_claim_instance(self->i2c_bus);
	if (sc != SL_STATUS_OK) {
		return sc;
	}

	sc = sl_i2c_set_transfer_complete_callback(
	    self->i2c_bus,
	    &app_lps22df_on_i2c_transfer_complete
	);

	if (sc != SL_STATUS_OK) {
		i2c_unclaim_instance(self->i2c_bus);
		return sc;
	}
	sc = sl_i2c_set_event_callback(self->i2c_bus, &app_lps22df_on_i2c_event);
	if (sc != SL_STATUS_OK) {
		i2c_unclaim_instance(self->i2c_bus);
		return sc;
	}

	self->tx_callback  = cb;
	self->tx_user_data = user_data;

	sc = sl_i2c_leader_send_non_blocking(
	    self->i2c_bus,
	    self->address,
	    buffer,
	    count,
	    (void *)self
	);
	if (sc != SL_STATUS_OK) {
		i2c_unclaim_instance(self->i2c_bus);
		self->tx_callback  = NULL;
		self->tx_user_data = NULL;

		return sc;
	}
	return sc;
}

sl_status_t app_lps22df_read_blocking(
    app_lps22df_handle_t *self,
    uint8_t               start_reg,
    uint8_t               count,
    uint8_t              *buffer
) {
	sl_status_t sc = i2c_claim_instance(self->i2c_bus);
	if (sc != SL_STATUS_OK) {
		return sc;
	}
	self->reg_address_buffer = start_reg;
	sc                       = sl_i2c_leader_transfer_blocking(
        self->i2c_bus,
        self->address,
        &self->reg_address_buffer,
        1,
        buffer,
        count,
        count
    );

	i2c_unclaim_instance(self->i2c_bus);

	return sc;
}

sl_status_t app_lps22df_write_blocking(
    app_lps22df_handle_t *self, uint8_t count, const uint8_t *buffer
) {
	if (count < 2) {
		return SL_STATUS_INVALID_COUNT;
	}

	sl_status_t sc = i2c_claim_instance(self->i2c_bus);
	if (sc != SL_STATUS_OK) {
		return sc;
	}

	sc = sl_i2c_leader_send_blocking(
	    self->i2c_bus,
	    self->address,
	    buffer,
	    count,
	    count
	);

	i2c_unclaim_instance(self->i2c_bus);

	return sc;
}

void app_lps22df_oneshot_complete(
    app_lps22df_handle_t *self, sl_status_t status, pressure_t pressure
) {
	app_lps22df_readout_callback_t cb;
	void                          *user_data;

	CORE_ATOMIC_SECTION({
		cb                      = self->oneshot_callback;
		user_data               = (void *)self->oneshot_user_data;
		self->oneshot_callback  = NULL;
		self->oneshot_user_data = NULL;
	});

	cb(status, pressure, user_data);
}

void app_lps22df_oneshot_read_cb(sl_status_t status, void *user_data) {
	app_lps22df_handle_t *self = user_data;

	if (status != SL_STATUS_OK) {
		app_lps22df_oneshot_complete(self, status, 0xffffffff);
		return;
	}

	int32_t pressure_data = ((uint32_t)self->read_buffer[2] << 16) |
	                        ((uint32_t)self->read_buffer[1] << 8) |
	                        ((uint32_t)self->read_buffer[0]);
	// data is 24bit signed in 2 complement, we do not support negative pressure
	if ((pressure_data & 0x00800000) != 0x00) {
		app_lps22df_oneshot_complete(self, SL_STATUS_INVALID_RANGE, 0xffffffff);
		return;
	}

	// data sensitivity is 4096 LSB/ hPA, pressure is in dPa
	float pressure_dPa = ((float)pressure_data) / 4.096f;
	app_lps22df_oneshot_complete(self, SL_STATUS_OK, (pressure_t)pressure_dPa);
}

void gpio_callback(uint8_t interrupt_number, void *user_data) {
	app_lps22df_handle_t *self = user_data;
	CORE_DECLARE_IRQ_STATE;
	CORE_ENTER_ATOMIC();
	if (interrupt_number != self->interrupt_number ||
	    self->oneshot_callback == NULL) {
		// nothing to do
		CORE_EXIT_ATOMIC();
		return;
	}
	CORE_EXIT_ATOMIC();

	sl_status_t sc = app_lps22df_read(
	    self,
	    0x28,
	    3,
	    self->read_buffer,
	    &app_lps22df_oneshot_read_cb,
	    (void *)self
	);

	if (sc != SL_STATUS_OK) {
		app_lps22df_oneshot_complete(self, sc, 0xffffffff);
	}
}

void app_lps22df_oneshot_write_cb(sl_status_t status, void *user_data) {
	if (status == SL_STATUS_OK) {
		// nothing todo, waiting INT.
		return;
	}
	// we could not write the command, terminate the async call
	app_lps22df_handle_t *self = user_data;
	app_lps22df_oneshot_complete(self, status, 0xffffffff);
}

sl_status_t app_lps22df_oneshot(
    app_lps22df_handle_t          *self,
    app_lps22df_readout_callback_t cb,
    void                          *user_data
) {
	CORE_DECLARE_IRQ_STATE;
	CORE_ENTER_ATOMIC();
	if (self->oneshot_callback != NULL || self->oneshot_user_data != NULL) {
		CORE_EXIT_ATOMIC();
		return SL_STATUS_BUSY;
	}

	self->oneshot_callback  = cb;
	self->oneshot_user_data = user_data;
	CORE_EXIT_ATOMIC();

	static const uint8_t oneshot_command[2] = {0x11, 0x01};

	sl_status_t s = app_lps22df_write(
	    self,
	    2,
	    oneshot_command,
	    &app_lps22df_oneshot_write_cb,
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

sl_status_t
app_lps22df_init(app_lps22df_handle_t *self, app_lps22df_config_t *config) {
	if (self == NULL || config == NULL || config->i2c_bus == NULL ||
	    config->drdy == NULL) {
		return SL_STATUS_NULL_POINTER;
	}

	self->i2c_bus = config->i2c_bus;
	self->address = config->addrLSBSet ? 0x5d : 0x5c;
	self->dready  = config->drdy;

	self->tx_callback       = NULL;
	self->tx_user_data      = NULL;
	self->oneshot_callback  = NULL;
	self->oneshot_user_data = NULL;

	uint8_t     whoAmI;
	sl_status_t sc = app_lps22df_read_blocking(self, 0x0f, 1, &whoAmI);
	if (sc != SL_STATUS_OK) {
		app_log_warning(
		    "No LPS22DF devices at %s.0x%x found, retrying in 80ms" APP_LOG_NL,
		    i2c_get_instance_name(self->i2c_bus),
		    self->address
		);

		sl_sleeptimer_delay_millisecond(80);
		sc = app_lps22df_read_blocking(self, 0x0f, 1, &whoAmI);

		if (sc != SL_STATUS_OK) {
			app_log_error(
			    "No LPS22DF devices at %s.0x%x found" APP_LOG_NL,
			    i2c_get_instance_name(self->i2c_bus),
			    self->address
			);
			return SL_STATUS_INITIALIZATION;
		}
	}

	if (whoAmI != 0x5c) {
		app_log_error(
		    "LPS2DF device found at %s.0x%x, but wrong whoAmI value 0x%x "
		    "(expected 0x5c)" APP_LOG_NL,
		    i2c_get_instance_name(self->i2c_bus),
		    self->address,
		    whoAmI
		);
		return SL_STATUS_INITIALIZATION;
	}

	app_log_info(
	    "found LPS22DF devices at %s.0x%x" APP_LOG_NL,
	    i2c_get_instance_name(self->i2c_bus),
	    self->address
	);

	uint8_t config_buffer[5] = {
	    0x10,
	    config->average, // reg 0x10, oneshot mode and AVG set by user
	    0x00,            // reg 0x11, default value, no command, idle mode
	    0x01,            // reg 0x12: IF_ADD_INC set
	    0x10,            // reg 0x13: DRDY on INT pint
	};

	sc = app_lps22df_write_blocking(self, sizeof(config_buffer), config_buffer);
	if (sc != SL_STATUS_OK) {
		app_log_error(
		    "LPS22DF %s.0x%x: could not set config" APP_LOG_NL,
		    i2c_get_instance_name(self->i2c_bus),
		    self->address
		);
		return SL_STATUS_INITIALIZATION;
	}

	sc = sl_gpio_set_pin_mode(self->dready, SL_GPIO_MODE_INPUT_PULL, false);
	if (sc != SL_STATUS_OK) {
		app_log_error(
		    "LPS22DF %s.0x%x: could not set  pin mode" APP_LOG_NL,
		    i2c_get_instance_name(self->i2c_bus),
		    self->address
		);
		return SL_STATUS_INITIALIZATION;
	}
	sc = sl_gpio_configure_external_interrupt(
	    self->dready,
	    &self->interrupt_number,
	    SL_GPIO_INTERRUPT_RISING_EDGE,
	    &gpio_callback,
	    self
	);
	if (sc != SL_STATUS_OK) {
		app_log_error(
		    "LPS22DF %s.0x%x: could not set pin interrupt" APP_LOG_NL,
		    i2c_get_instance_name(self->i2c_bus),
		    self->address
		);
		return SL_STATUS_INITIALIZATION;
	}

	return SL_STATUS_OK;
}
