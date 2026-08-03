#include "stcc4.h"
#include "app_log.h"
#include "drivers/i2c_schd.h"
#include "sl_core.h"
#include "sl_enum.h"
#include "sl_gpio.h"
#include "sl_sleeptimer.h"
#include "sl_status.h"
#include "types.h"
#include "utils/crc8.h"
#include <stdint.h>

SL_ENUM_GENERIC(_stcc4_command_t, uint16_t){
    stcc4_start_continuous_measurement = 0x218B,
    stcc4_stop_continuous_measurement  = 0x3F86,
    stcc4_read_measurement             = 0xEC05,
    stcc4_set_rht_compensation         = 0xE000,
    stcc4_set_pressure_compensation    = 0xE016,
    stcc4_measure_single_shot          = 0x219D,
    stcc4_enter_sleep_mode             = 0x3650,
    stcc4_exit_sleep_mode              = 0x0000,
    stcc4_perform_conditioning         = 0x29BC,
    stcc4_perform_soft_reset           = 0x0006,
    stcc4_perform_factory_reset        = 0x3632,
    stcc4_perform_self_test            = 0x278C,
    stcc4_enable_testing_mode          = 0x3FBC,
    stcc4_disable_testing_mode         = 0x3F3D,
    stcc4_perform_forced_recalibration = 0x362F,
    stcc4_get_product_ID               = 0x365B,
};

/// this function has an inverted signature to be used also as a callback
void _stcc4_complete_tx(i2c_tx_status_t status, void *user_data) {
	stcc4_handle_t   *self = user_data;
	i2c_tx_callback_t callback;
	CORE_ATOMIC_SECTION({
		callback           = self->tx_callback;
		user_data          = (void *)self->tx_user_data;
		self->tx_callback  = NULL;
		self->tx_user_data = NULL;
	});

	if (callback != NULL) {
		callback(status, user_data);
	}
}

void _stcc4_tx_timer_timeout(
    sl_sleeptimer_timer_handle_t *timer, void *user_data
) {
	(void)timer;
	stcc4_handle_t *self = user_data;
	if (self->read_len == 0) {
		_stcc4_complete_tx(I2C_TX_OK, self);
		return;
	}
	sl_status_t status = i2c_schd_receive(
	    self->i2c_bus,
	    self->address,
	    self->buffer,
	    self->read_len,
	    &_stcc4_complete_tx,
	    self
	);
	if (status != SL_STATUS_OK) {
		_stcc4_complete_tx(I2C_TX_START_ERROR, self);
	}
}

void _stcc4_on_command_write(i2c_tx_status_t status, void *user_data) {
	stcc4_handle_t *self = user_data;
	if (self->buffer[0] == 0x00) {
		if (status != I2C_TX_BUS_ERROR) {
			app_log_debug(
			    "[STCC4] Got unexpected status on exit sleep mode "
			    "0x%02X." APP_LOG_NL,
			    status
			);
		}
	} else if (status != I2C_TX_OK ||
	           (self->read_len == 0 && self->read_delay_ms == 0)) {
		_stcc4_complete_tx(status, self);
		return;
	}

	sl_status_t command_status;
	if (self->read_delay_ms != 0) {
		command_status = sl_sleeptimer_start_timer_ms(
		    &self->timer,
		    self->read_delay_ms,
		    &_stcc4_tx_timer_timeout,
		    self,
		    0,
		    0
		);
	} else {
		command_status = i2c_schd_receive(
		    self->i2c_bus,
		    self->address,
		    self->buffer,
		    self->read_len,
		    &_stcc4_complete_tx,
		    self
		);
	}
	if (command_status != SL_STATUS_OK) {
		_stcc4_complete_tx(I2C_TX_START_ERROR, self);
	}
}

sl_status_t _stcc4_send_command(
    stcc4_handle_t   *self,
    _stcc4_command_t  cmd,
    uint8_t           command_len,
    uint16_t          read_delay_ms,
    uint8_t           read_len,
    i2c_tx_callback_t callback,
    void             *user_data
) {
	if (self == NULL) {
		return SL_STATUS_NULL_POINTER;
	}

	if (command_len == 0) {
		return SL_STATUS_INVALID_PARAMETER;
	}

	CORE_DECLARE_IRQ_STATE;
	CORE_ENTER_ATOMIC();
	if (self->tx_callback != NULL) {
		CORE_EXIT_ATOMIC();
		return SL_STATUS_BUSY;
	}
	self->tx_callback   = callback;
	self->tx_user_data  = user_data;
	self->read_delay_ms = read_delay_ms;
	self->read_len      = read_len;
	CORE_EXIT_ATOMIC();
	self->buffer[0] = cmd >> 8;
	if (command_len > 1) {
		self->buffer[1] = cmd & 0xff;
	}
	sl_status_t status = i2c_schd_send(
	    self->i2c_bus,
	    self->address,
	    self->buffer,
	    command_len,
	    &_stcc4_on_command_write,
	    self
	);
	if (status != SL_STATUS_OK) {
		CORE_ENTER_ATOMIC();
		self->tx_callback  = NULL;
		self->tx_user_data = NULL;
		CORE_EXIT_ATOMIC();
	}

	return status;
}

sl_status_t _stcc4_send_command_blocking(
    stcc4_handle_t  *self,
    _stcc4_command_t cmd,
    uint16_t         read_delay_ms,
    uint8_t         *read_buffer,
    uint8_t          read_len
) {

	CORE_DECLARE_IRQ_STATE;
	CORE_ENTER_ATOMIC();
	if (self->read_callback != NULL || self->tx_callback != NULL) {
		CORE_EXIT_ATOMIC();
		return SL_STATUS_BUSY;
	}
	CORE_EXIT_ATOMIC();
	self->buffer[0] = cmd >> 8;
	self->buffer[1] = cmd & 0xff;

	sl_status_t status =
	    i2c_schd_send_blocking(self->i2c_bus, self->address, self->buffer, 2);
	if (status != SL_STATUS_OK || read_len == 0) {
		return status;
	}
	if (read_delay_ms > 0) {
		sl_sleeptimer_delay_millisecond(read_delay_ms);
	}
	return i2c_schd_receive_blocking(
	    self->i2c_bus,
	    self->address,
	    read_buffer,
	    read_len
	);
}

// checks a 16 bit word CRC value.
bool _stcc4_check_crc_word(const uint8_t *buffer) {
	uint8_t crc = 0xff;
	for (size_t i = 0; i < 3; ++i) {
		crc = CRC8_AppendByte(crc, 0x31, buffer[i]);
	}
	return crc == 0x00;
}

sl_status_t _stcc4_read_serial_number_blocking(
    stcc4_handle_t *self, uint32_t *serial_number
) {
	uint8_t     buffer[6];
	sl_status_t status =
	    _stcc4_send_command_blocking(self, stcc4_get_product_ID, 1, buffer, 6);
	if (status != SL_STATUS_OK) {
		return status;
	}
	bool crc_ok =
	    _stcc4_check_crc_word(buffer) && _stcc4_check_crc_word(&buffer[3]);
	if (crc_ok == false) {
		return SL_STATUS_INVALID_COUNT;
	}
	*serial_number = ((uint32_t)buffer[3] << 24) | ((uint32_t)buffer[2] << 16) |
	                 ((uint32_t)buffer[1] << 8) | ((uint32_t)buffer[0] << 0);
	return SL_STATUS_OK;
}

sl_status_t stcc4_init(stcc4_handle_t *self, stcc4_init_args_t *args) {
	if (self == NULL || args == NULL || args->i2c_bus == NULL) {
		return SL_STATUS_NULL_POINTER;
	}

	self->i2c_bus       = args->i2c_bus;
	self->tx_callback   = NULL;
	self->read_callback = NULL;
	if (args->address_pin_set == true) {
		self->address = 0x65;
	} else {
		self->address = 0x64;
	}

	uint32_t    serial_number;
	sl_status_t status =
	    _stcc4_read_serial_number_blocking(self, &serial_number);
	if (status != SL_STATUS_OK) {
		app_log_warning(
		    "[STCC4] No device found at %s.0x%02X, retrying in 80ms" APP_LOG_NL,
		    i2c_schd_get_instance_name(self->i2c_bus),
		    self->address
		);
		sl_sleeptimer_delay_millisecond(80);
		status = _stcc4_read_serial_number_blocking(self, &serial_number);
	}
	if (status != SL_STATUS_OK) {
		app_log_error(
		    "[STCC4] No device found at %s.0x%02X." APP_LOG_NL,
		    i2c_schd_get_instance_name(self->i2c_bus),
		    self->address
		);
		return status;
	}

	app_log_info(
	    "[STCC4] device found at %s.0x%02X. Entering deep-sleep" APP_LOG_NL,
	    i2c_schd_get_instance_name(self->i2c_bus),
	    self->address
	);
	status =
	    _stcc4_send_command_blocking(self, stcc4_enter_sleep_mode, 1, NULL, 0);
	if (status != SL_STATUS_OK) {
		app_log_error(
		    "[STCC4] device found at %s.0x%02X. But could not enter deep "
		    "sleep: 0x%04lX" APP_LOG_NL,
		    i2c_schd_get_instance_name(self->i2c_bus),
		    self->address,
		    status
		);
	}

	return status;
}

void _stcc4_on_enter_sleepmode(i2c_tx_status_t status, void *user_data) {
	(void)user_data;
	if (status != I2C_TX_OK) {
		app_log_error(
		    "[STCC4] could not enter sleepmode: 0x%02X" APP_LOG_NL,
		    status
		);
	}
}

void _stcc4_complete_read_sequence(
    stcc4_handle_t *self, sl_status_t status, co2_concentration_t co2
) {
	stcc4_readout_callback_t callback;
	void                    *user_data;
	CORE_ATOMIC_SECTION({
		callback             = self->read_callback;
		user_data            = (void *)self->read_user_data;
		self->read_callback  = NULL;
		self->read_user_data = NULL;
	});
	if (callback == NULL) {
		return;
	}

	sl_status_t sleep_status = _stcc4_send_command(
	    self,
	    stcc4_enter_sleep_mode,
	    2,
	    1,
	    0,
	    &_stcc4_on_enter_sleepmode,
	    self
	);
	if (sleep_status != SL_STATUS_OK) {
		app_log_error(
		    "[STCC4] could not schedule sleepmode: 0x%04lX" APP_LOG_NL,
		    sleep_status
		);
	}

	callback(status, co2, user_data);
};

void _stcc4_on_read_measurement(i2c_tx_status_t status, void *user_data) {
	stcc4_handle_t *self = user_data;
	if (status != I2C_TX_OK) {
		_stcc4_complete_read_sequence(self, SL_STATUS_BUS_ERROR, GATT_CO2_NAN);
		return;
	}

	if (_stcc4_check_crc_word(self->buffer) == false) {
		_stcc4_complete_read_sequence(
		    self,
		    SL_STATUS_INVALID_COUNT,
		    GATT_CO2_NAN
		);
		return;
	}

	uint16_t pressure_ppm =
	    ((uint16_t)self->buffer[0] << 8) | ((uint16_t)self->buffer[1]);

	// check for saturation
	if (pressure_ppm == GATT_CO2_NAN) {
		pressure_ppm = GATT_CO2_MAX;
	}

	_stcc4_complete_read_sequence(self, SL_STATUS_OK, pressure_ppm);
}

void _stcc4_on_measure_single_shot(i2c_tx_status_t status, void *user_data) {
	stcc4_handle_t *self = user_data;
	if (status != I2C_TX_OK) {
		_stcc4_complete_read_sequence(self, SL_STATUS_BUS_ERROR, 0xffff);
		return;
	}

	sl_status_t command_status = _stcc4_send_command(
	    self,
	    stcc4_read_measurement,
	    2,
	    1,
	    12,
	    &_stcc4_on_read_measurement,
	    self
	);

	if (command_status != SL_STATUS_OK) {
		_stcc4_complete_read_sequence(self, command_status, GATT_CO2_NAN);
	}
}

void _stcc4_on_set_pressure_compensation(
    i2c_tx_status_t status, void *user_data
) {
	stcc4_handle_t *self = user_data;
	if (status != I2C_TX_OK) {
		_stcc4_complete_read_sequence(self, SL_STATUS_BUS_ERROR, 0xffff);
		return;
	}

	sl_status_t command_status = _stcc4_send_command(
	    self,
	    stcc4_measure_single_shot,
	    2,
	    500,
	    0,
	    &_stcc4_on_measure_single_shot,
	    self
	);

	if (command_status != SL_STATUS_OK) {
		_stcc4_complete_read_sequence(self, command_status, GATT_CO2_NAN);
	}
}

void _stcc4_on_set_rht_compensation(i2c_tx_status_t status, void *user_data) {
	stcc4_handle_t *self = user_data;
	if (status != I2C_TX_OK) {
		_stcc4_complete_read_sequence(self, SL_STATUS_BUS_ERROR, GATT_CO2_NAN);
		return;
	}
	uint16_t pressure;
	if (self->pressure >= 1100000) {
		pressure = 55000;
	} else if (self->pressure <= 400000) {
		pressure = 20000;
	} else {
		pressure = self->pressure / 20;
	}

	self->buffer[2] = pressure >> 8;
	self->buffer[3] = pressure & 0xff;
	self->buffer[4] = 0xff;
	self->buffer[4] = CRC8_AppendByte(self->buffer[4], 0x31, self->buffer[2]);
	self->buffer[4] = CRC8_AppendByte(self->buffer[4], 0x31, self->buffer[3]);

	sl_status_t command_status = _stcc4_send_command(
	    self,
	    stcc4_set_pressure_compensation,
	    5,
	    1,
	    0,
	    &_stcc4_on_set_pressure_compensation,
	    self
	);
	if (command_status != SL_STATUS_OK) {
		_stcc4_complete_read_sequence(self, command_status, GATT_CO2_NAN);
	}
}

void _stcc4_on_exit_sleepmode(i2c_tx_status_t status, void *user_data) {
	stcc4_handle_t *self = user_data;
	if (status != I2C_TX_OK) {
		_stcc4_complete_read_sequence(self, SL_STATUS_BUS_ERROR, GATT_CO2_NAN);
		return;
	}

	uint16_t temperature =
	    ((float)self->temperature + 4500.0f) / 17500.0f * 65535.0f;

	uint16_t humidity = ((float)self->humidity + 60.0f) / 1250.0f * 65535.0f;

	self->buffer[2] = temperature >> 8;
	self->buffer[3] = temperature & 0xff;
	self->buffer[4] = 0xff;
	self->buffer[4] = CRC8_AppendByte(self->buffer[4], 0x31, self->buffer[2]);
	self->buffer[4] = CRC8_AppendByte(self->buffer[4], 0x31, self->buffer[3]);

	self->buffer[5] = humidity >> 8;
	self->buffer[6] = humidity & 0xff;
	self->buffer[7] = 0xff;
	self->buffer[7] = CRC8_AppendByte(self->buffer[7], 0x31, self->buffer[5]);
	self->buffer[7] = CRC8_AppendByte(self->buffer[7], 0x31, self->buffer[6]);

	sl_status_t command_status = _stcc4_send_command(
	    self,
	    stcc4_set_rht_compensation,
	    8,
	    1,
	    0,
	    &_stcc4_on_set_rht_compensation,
	    self
	);
	if (command_status != SL_STATUS_OK) {
		_stcc4_complete_read_sequence(self, command_status, GATT_CO2_NAN);
	}
}

sl_status_t _stcc4_send_exit_sleep_mode(stcc4_handle_t *self) {
	return _stcc4_send_command(
	    self,
	    stcc4_exit_sleep_mode,
	    1,
	    5,
	    0,
	    _stcc4_on_exit_sleepmode,
	    self
	);
}

sl_status_t stcc4_start_read_sequence(
    stcc4_handle_t          *self,
    temperature_t            temperature,
    humidity_t               humidity,
    pressure_t               pressure,
    stcc4_readout_callback_t callback,
    void                    *user_data
) {
	if (self == NULL) {
		return SL_STATUS_NULL_POINTER;
	}
	if (temperature == GATT_TEMPERATURE_NAN || humidity == GATT_HUMIDITY_NAN ||
	    pressure == GATT_PRESSURE_NAN) {
		return SL_STATUS_INVALID_PARAMETER;
	}

	CORE_DECLARE_IRQ_STATE;
	CORE_ENTER_ATOMIC();
	if (self->read_callback != NULL || self->tx_callback != NULL) {
		CORE_EXIT_ATOMIC();
		return SL_STATUS_BUSY;
	}
	self->read_callback  = callback;
	self->read_user_data = user_data;
	CORE_EXIT_ATOMIC();
	self->temperature = temperature;
	self->humidity    = humidity;
	self->pressure    = pressure;

	sl_status_t status = _stcc4_send_exit_sleep_mode(self);

	if (status != SL_STATUS_OK) {
		CORE_ENTER_ATOMIC();
		self->read_callback  = NULL;
		self->read_user_data = NULL;
		CORE_EXIT_ATOMIC();
	}

	return status;
}
