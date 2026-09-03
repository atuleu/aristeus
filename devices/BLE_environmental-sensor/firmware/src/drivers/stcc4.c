#include "stcc4.h"
#include "app_config.h"
#include "app_log.h"
#include "drivers/i2c_schd.h"
#include "sl_core.h"
#include "sl_enum.h"
#include "sl_sleeptimer.h"
#include "sl_status.h"
#include "types.h"
#include "utils/crc8.h"
#include "utils/status.h"
#include <stdint.h>

#define SL_STATUS_NO_STATUS 0xFFFF

SL_ENUM_GENERIC(_stcc4_command_t, uint16_t){
    stcc4_cmd_start_continuous_measurement = 0x218B,
    stcc4_cmd_stop_continuous_measurement  = 0x3F86,
    stcc4_cmd_read_measurement             = 0xEC05,
    stcc4_cmd_set_rht_compensation         = 0xE000,
    stcc4_cmd_set_pressure_compensation    = 0xE016,
    stcc4_cmd_measure_single_shot          = 0x219D,
    stcc4_cmd_enter_sleep_mode             = 0x3650,
    stcc4_cmd_exit_sleep_mode              = 0x0000,
    stcc4_cmd_perform_conditioning         = 0x29BC,
    stcc4_cmd_perform_soft_reset           = 0x0006,
    stcc4_cmd_perform_factory_reset        = 0x3632,
    stcc4_cmd_perform_self_test            = 0x278C,
    stcc4_cmd_enable_testing_mode          = 0x3FBC,
    stcc4_cmd_disable_testing_mode         = 0x3F3D,
    stcc4_cmd_perform_forced_recalibration = 0x362F,
    stcc4_cmd_get_product_ID               = 0x365B,
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
	if (self->buffer[0] == stcc4_cmd_exit_sleep_mode) {
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
		    SL_SLEEPTIMER_NO_HIGH_PRECISION_HF_CLOCKS_REQUIRED_FLAG
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
	sl_status_t status = _stcc4_send_command_blocking(
	    self,
	    stcc4_cmd_get_product_ID,
	    1,
	    buffer,
	    6
	);
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
	self->op_callback   = NULL;
	self->sleeping      = false;
	self->last_readout  = GATT_CO2_NAN;
	self->last_status   = SL_STATUS_NO_STATUS;
	if (args->address_pin_set == true) {
		self->address = 0x65;
	} else {
		self->address = 0x64;
	}

	uint32_t    serial_number;
	sl_status_t status;

	for (uint8_t i = 0; i < (DEVICE_INIT_CONNECT_MAX_TRIES - 1); ++i) {
		status = _stcc4_read_serial_number_blocking(self, &serial_number);
		if (status == SL_STATUS_OK) {
			break;
		}
		app_log_warning(
		    "[STCC4] No device found at %s.0x%02X, retrying in %dms, reason: "
		    "%s" APP_LOG_NL,
		    i2c_schd_get_instance_name(self->i2c_bus),
		    self->address,
		    DEVICE_CONNECT_RETRIES_TIMEOUT_MS,
		    sl_status_get_string(status)
		);
		sl_sleeptimer_delay_millisecond(DEVICE_CONNECT_RETRIES_TIMEOUT_MS);
	}

	if (status != SL_STATUS_OK) {
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

	app_log_debug(
	    "[STCC4] device found at %s.0x%02X." APP_LOG_NL,
	    i2c_schd_get_instance_name(self->i2c_bus),
	    self->address
	);

	return SL_STATUS_OK;
}

void _stcc4_on_enter_sleepmode(i2c_tx_status_t status, void *user_data);
void _stcc4_complete_any_pending(stcc4_handle_t *self);

static inline sl_status_t
_stcc4_enter_sleep_mode(stcc4_handle_t *self, CORE_irqState_t irqState) {
	if (self->sleeping == true) {
		CORE_EXIT_ATOMIC();
		return SL_STATUS_OK;
	}
	if (self->tx_callback != NULL) {
		CORE_EXIT_ATOMIC();
		return SL_STATUS_BUSY;
	}

	sl_status_t status = _stcc4_send_command(
	    self,
	    stcc4_cmd_enter_sleep_mode,
	    2,
	    1,
	    0,
	    &_stcc4_on_enter_sleepmode,
	    self
	);
	CORE_EXIT_ATOMIC();

	if (status != SL_STATUS_OK) {
		app_log_error(
		    "[STCC4] could not schedule sleepmode: %s" APP_LOG_NL,
		    sl_status_get_string(status)
		);
	}
	return status;
}

sl_status_t stcc4_enter_sleep_mode(stcc4_handle_t *self) {
	CORE_DECLARE_IRQ_STATE;
	CORE_ENTER_ATOMIC();
	if (self->read_callback != NULL || self->op_callback != NULL) {
		CORE_EXIT_ATOMIC();
		return SL_STATUS_BUSY;
	}
	// the later will release atomic.
	return _stcc4_enter_sleep_mode(self, irqState);
}

void _stcc4_complete_read_sequence(
    stcc4_handle_t *self, sl_status_t status, co2_concentration_t co2
);

void _stcc4_schedule_read_completion(
    stcc4_handle_t *self, sl_status_t status, co2_concentration_t co2
) {
	CORE_DECLARE_IRQ_STATE;
	CORE_ENTER_ATOMIC();

	self->last_status        = status;
	self->last_readout       = co2;
	// the later will release Atomic
	sl_status_t sleep_status = _stcc4_enter_sleep_mode(self, irqState);

	if (sleep_status != SL_STATUS_OK) {
		CORE_ATOMIC_SECTION({
			self->last_status  = SL_STATUS_NO_STATUS;
			self->last_readout = GATT_CO2_NAN;
		});
		_stcc4_complete_read_sequence(self, status, co2);
	}
}

void _stcc4_complete_read_sequence(
    stcc4_handle_t *self, sl_status_t status, co2_concentration_t co2
) {
	stcc4_readout_callback_t callback;
	void                    *user_data;
	CORE_ATOMIC_SECTION({
		callback            = self->read_callback;
		user_data           = (void *)self->user_data;
		self->read_callback = NULL;
		self->user_data     = NULL;
	});

	if (callback == NULL) {
		return;
	}

	callback(status, co2, user_data);
};

void _stcc4_schedule_complete_operation(
    stcc4_handle_t *self, sl_status_t status
);

void _stcc4_complete_operation(stcc4_handle_t *self, sl_status_t status) {
	stcc4_operation_callback_t callback;
	void                      *user_data;
	CORE_ATOMIC_SECTION({
		callback          = self->op_callback;
		user_data         = (void *)self->user_data;
		self->op_callback = NULL;
		self->user_data   = NULL;
	});

	if (callback == NULL) {
		return;
	}

	callback(status, user_data);
}

void _stcc4_schedule_complete_operation(
    stcc4_handle_t *self, sl_status_t status
) {
	CORE_DECLARE_IRQ_STATE;
	CORE_ENTER_ATOMIC();
	self->last_status        = status;
	// the later will drop atomicity
	sl_status_t sleep_status = _stcc4_enter_sleep_mode(self, irqState);

	if (sleep_status != SL_STATUS_OK) {
		CORE_ATOMIC_SECTION({ self->last_status = SL_STATUS_NO_STATUS; });
		_stcc4_complete_operation(self, status);
	}
}

void _stcc4_on_enter_sleepmode(i2c_tx_status_t status, void *user_data) {
	stcc4_handle_t *self = user_data;
	if (status != I2C_TX_OK) {
		app_log_error(
		    "[STCC4] could not enter sleepmode: 0x%02X" APP_LOG_NL,
		    status
		);
	} else {
		CORE_ATOMIC_SECTION({ self->sleeping = true; });
	}

	_stcc4_complete_any_pending(self);
}

void _stcc4_complete_any_pending(stcc4_handle_t *self) {
	CORE_DECLARE_IRQ_STATE;
	CORE_ENTER_ATOMIC();
	sl_status_t         rs = self->last_status;
	co2_concentration_t rv = self->last_readout;
	self->last_status      = SL_STATUS_NO_STATUS;
	self->last_readout     = GATT_CO2_NAN;
	CORE_EXIT_ATOMIC();
	if (rs != SL_STATUS_NO_STATUS) {
		if (self->read_callback != NULL) {
			_stcc4_complete_read_sequence(self, rs, rv);
		} else {
			_stcc4_complete_operation(self, rs);
		}
	}
}

void _stcc4_on_read_measurement(i2c_tx_status_t status, void *user_data) {
	stcc4_handle_t *self = user_data;
	if (status != I2C_TX_OK) {
		_stcc4_schedule_read_completion(
		    self,
		    SL_STATUS_BUS_ERROR,
		    GATT_CO2_NAN
		);
		return;
	}
#ifndef PRODUCTION_BUILD
	for (size_t i = 0; i < 12; i += 3) {
#else
	for (size_t i = 0; i < 12; i += 9) {
#endif // PRODUCTION_BUILD
		if (_stcc4_check_crc_word(&self->buffer[i]) == false) {
			_stcc4_schedule_read_completion(
			    self,
			    SL_STATUS_INVALID_COUNT,
			    GATT_CO2_NAN
			);
			return;
		}
	}

	uint16_t co2_ppm =
	    ((uint16_t)self->buffer[0] << 8) | ((uint16_t)self->buffer[1]);

	uint16_t sensor_status =
	    ((uint16_t)self->buffer[9] << 8) | ((uint16_t)self->buffer[10]);

#ifndef PRODUCTION_BUILD

	uint16_t temperature_B =
	    ((uint16_t)self->buffer[3] << 8) | ((uint16_t)self->buffer[4]);

	uint16_t humidity_B =
	    ((uint16_t)self->buffer[6] << 8) | ((uint16_t)self->buffer[7]);

	uint16_t temperature_C =
	    ((float)temperature_B) / 65535.0f * 17500.0f - 4500.0f;

	uint16_t humidity_percent =
	    ((float)humidity_B) / 65535.0f * 1250.0f - 60.0f;

	app_log_debug(
	    "[stcc4] got status: %04X temp=%d.%02d°C (%d) humidity=%d.%d "
	    "(%d)." APP_LOG_NL,
	    sensor_status,
	    temperature_C / 100,
	    temperature_C % 100,
	    temperature_B,
	    humidity_percent / 10,
	    humidity_percent % 10,
	    humidity_B
	);
#endif // PRODUCTION_BUILD

	if (sensor_status != 0x04) {
		_stcc4_schedule_read_completion(self, SL_STATUS_FAIL, GATT_CO2_NAN);
	} else {
		_stcc4_schedule_read_completion(self, SL_STATUS_OK, co2_ppm);
	}
}

void _stcc4_on_measure_single_shot(i2c_tx_status_t status, void *user_data) {
	stcc4_handle_t *self = user_data;
	if (status != I2C_TX_OK) {
		_stcc4_schedule_read_completion(self, SL_STATUS_BUS_ERROR, 0xffff);
		return;
	}

	sl_status_t command_status = _stcc4_send_command(
	    self,
	    stcc4_cmd_read_measurement,
	    2,
	    1,
	    12,
	    &_stcc4_on_read_measurement,
	    self
	);

	if (command_status != SL_STATUS_OK) {
		_stcc4_schedule_read_completion(self, command_status, GATT_CO2_NAN);
	}
}

void _stcc4_on_set_pressure_compensation(
    i2c_tx_status_t status, void *user_data
) {
	stcc4_handle_t *self = user_data;
	if (status != I2C_TX_OK) {
		_stcc4_schedule_read_completion(self, SL_STATUS_BUS_ERROR, 0xffff);
		return;
	}

	sl_status_t command_status = _stcc4_send_command(
	    self,
	    stcc4_cmd_measure_single_shot,
	    2,
	    500,
	    0,
	    &_stcc4_on_measure_single_shot,
	    self
	);

	if (command_status != SL_STATUS_OK) {
		_stcc4_schedule_read_completion(self, command_status, GATT_CO2_NAN);
	}
}

void _stcc4_on_set_rht_compensation(i2c_tx_status_t status, void *user_data) {
	stcc4_handle_t *self = user_data;
	if (status != I2C_TX_OK) {
		_stcc4_schedule_read_completion(
		    self,
		    SL_STATUS_BUS_ERROR,
		    GATT_CO2_NAN
		);
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

#ifndef PRODUCTION_BUILD
	app_log_debug(
	    "[STCC4] setting barometric pressure=%d (%ld.%03ld hPa)." APP_LOG_NL,
	    pressure,
	    self->pressure / 1000,
	    self->pressure % 1000
	);
#endif // PRODUCTION_BUILD
	self->buffer[2] = pressure >> 8;
	self->buffer[3] = pressure & 0xff;
	self->buffer[4] = 0xff;
	self->buffer[4] = CRC8_AppendByte(self->buffer[4], 0x31, self->buffer[2]);
	self->buffer[4] = CRC8_AppendByte(self->buffer[4], 0x31, self->buffer[3]);

	sl_status_t command_status = _stcc4_send_command(
	    self,
	    stcc4_cmd_set_pressure_compensation,
	    5,
	    1,
	    0,
	    &_stcc4_on_set_pressure_compensation,
	    self
	);
	if (command_status != SL_STATUS_OK) {
		_stcc4_schedule_read_completion(self, command_status, GATT_CO2_NAN);
	}
}

void _stcc4_start_readout_sequence(i2c_tx_status_t status, void *user_data) {
	stcc4_handle_t *self = user_data;
	if (status != I2C_TX_OK) {
		_stcc4_schedule_read_completion(
		    self,
		    SL_STATUS_BUS_ERROR,
		    GATT_CO2_NAN
		);
		return;
	}

	uint16_t temperature =
	    ((float)(self->temperature + 4500)) * 65535.0f / 17500.0f;

	uint16_t humidity = ((float)self->humidity + 60.0f) / 1250.0f * 65535.0f;
#ifndef PRODUCTION_BUILD
	app_log_debug(
	    "[STCC4] sending compensation temp=%d humidity=%d." APP_LOG_NL,
	    temperature,
	    humidity
	);
#endif // PRODUCTION_BUILD
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
	    stcc4_cmd_set_rht_compensation,
	    8,
	    1,
	    0,
	    &_stcc4_on_set_rht_compensation,
	    self
	);
	if (command_status != SL_STATUS_OK) {
		_stcc4_schedule_read_completion(self, command_status, GATT_CO2_NAN);
	}
}

sl_status_t
_stcc4_send_exit_sleep_mode(stcc4_handle_t *self, i2c_tx_callback_t on_wakeup) {
	if (self->sleeping == false) {
		on_wakeup(I2C_TX_OK, self);
		return SL_STATUS_OK;
	}
	self->sleeping = false;
	return _stcc4_send_command(
	    self,
	    stcc4_cmd_exit_sleep_mode,
	    1,
	    5,
	    0,
	    on_wakeup,
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

	if (self->read_callback != NULL || self->tx_callback != NULL ||
	    self->op_callback != NULL) {
		CORE_EXIT_ATOMIC();
		return SL_STATUS_BUSY;
	}
	self->read_callback = callback;
	self->user_data     = user_data;
	CORE_EXIT_ATOMIC();
	self->temperature = temperature;
	self->humidity    = humidity;
	self->pressure    = pressure;

	sl_status_t status =
	    _stcc4_send_exit_sleep_mode(self, _stcc4_start_readout_sequence);

	if (status != SL_STATUS_OK) {
		CORE_ENTER_ATOMIC();
		self->read_callback = NULL;
		self->user_data     = NULL;
		CORE_EXIT_ATOMIC();
	}

	return status;
}

void _stcc4_on_perform_conditioning(i2c_tx_status_t status, void *user_data);

void _stcc4_start_conditioning(i2c_tx_status_t status, void *user_data) {
	stcc4_handle_t *self = user_data;
	if (status != I2C_TX_OK) {
		_stcc4_schedule_complete_operation(self, SL_STATUS_BUS_ERROR);
		return;
	}

	sl_status_t cmd_status = _stcc4_send_command(
	    self,
	    stcc4_cmd_perform_conditioning,
	    2,
	    22000,
	    0,
	    &_stcc4_on_perform_conditioning,
	    self
	);
	if (cmd_status != SL_STATUS_OK) {
		_stcc4_schedule_complete_operation(self, cmd_status);
	}
}

void _stcc4_on_perform_conditioning(i2c_tx_status_t status, void *user_data) {
	stcc4_handle_t *self = user_data;
	if (status != I2C_TX_OK) {
		_stcc4_schedule_complete_operation(self, SL_STATUS_BUS_ERROR);
		return;
	}
	_stcc4_schedule_complete_operation(self, SL_STATUS_OK);
}

sl_status_t stcc4_perform_conditioning(
    stcc4_handle_t *self, stcc4_operation_callback_t callback, void *user_data
) {
	if (self == NULL) {
		return SL_STATUS_NULL_POINTER;
	}
	CORE_DECLARE_IRQ_STATE;
	CORE_ENTER_ATOMIC();

	if (self->read_callback != NULL || self->tx_callback != NULL ||
	    self->op_callback != NULL) {
		CORE_EXIT_ATOMIC();
		return SL_STATUS_BUSY;
	}
	self->op_callback = callback;
	self->user_data   = user_data;
	CORE_EXIT_ATOMIC();

	sl_status_t status =
	    _stcc4_send_exit_sleep_mode(self, _stcc4_start_conditioning);

	if (status != SL_STATUS_OK) {
		CORE_ENTER_ATOMIC();
		self->op_callback = NULL;
		self->user_data   = NULL;
		CORE_EXIT_ATOMIC();
	}
	return status;
}

void _stcc4_on_factory_reset(i2c_tx_status_t status, void *user_data);

void _stcc4_start_factory_reset(i2c_tx_status_t status, void *user_data) {
	stcc4_handle_t *self = user_data;
	if (status != I2C_TX_OK) {
		_stcc4_schedule_complete_operation(self, SL_STATUS_BUS_ERROR);
		return;
	}

	sl_status_t cmd_status = _stcc4_send_command(
	    self,
	    stcc4_cmd_perform_factory_reset,
	    2,
	    90,
	    2,
	    &_stcc4_on_factory_reset,
	    self
	);
	if (cmd_status != SL_STATUS_OK) {
		_stcc4_schedule_complete_operation(self, cmd_status);
	}
}

void _stcc4_on_factory_reset(i2c_tx_status_t status, void *user_data) {
	stcc4_handle_t *self = user_data;
	if (status != I2C_TX_OK) {
		_stcc4_schedule_complete_operation(self, SL_STATUS_BUS_ERROR);
		return;
	}

	if (self->buffer[0] == 0x00 && self->buffer[1] == 0x00) {
		_stcc4_schedule_complete_operation(self, SL_STATUS_OK);
	} else if (self->buffer[0] == 0xFF && self->buffer[1] == 0xFF) {
		_stcc4_schedule_complete_operation(self, SL_STATUS_FAIL);
	} else {
		_stcc4_schedule_complete_operation(self, SL_STATUS_INVALID_KEY);
	}
}

sl_status_t stcc4_factory_reset(
    stcc4_handle_t *self, stcc4_operation_callback_t callback, void *user_data
) {
	if (self == NULL) {
		return SL_STATUS_NULL_POINTER;
	}
	CORE_DECLARE_IRQ_STATE;
	CORE_ENTER_ATOMIC();

	if (self->read_callback != NULL || self->tx_callback != NULL ||
	    self->op_callback != NULL) {
		CORE_EXIT_ATOMIC();
		return SL_STATUS_BUSY;
	}
	self->op_callback = callback;
	self->user_data   = user_data;
	CORE_EXIT_ATOMIC();

	sl_status_t status =
	    _stcc4_send_exit_sleep_mode(self, _stcc4_start_factory_reset);

	if (status != SL_STATUS_OK) {
		CORE_ENTER_ATOMIC();
		self->op_callback = NULL;
		self->user_data   = NULL;
		CORE_EXIT_ATOMIC();
	}
	return status;
}
