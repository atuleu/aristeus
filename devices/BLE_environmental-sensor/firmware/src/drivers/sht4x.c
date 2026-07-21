#include "sht4x.h"

#include <stdint.h>

#include <sl_i2c.h>
#include <sl_sleeptimer.h>
#include <sl_status.h>

#include <app_log.h>

#include "drivers/i2c_schd.h"
#include "sl_core.h"
#include "types.h"
#include <utils/crc8.h>

sl_status_t
sht4x_init(sht4x_handle_t *self, i2c_schd_handle_t *i2c, uint8_t addr) {
	sht4x_blocking_result_t result;
	self->i2c_bus = i2c;
	if (addr != 0x44 && addr != 0x45 && addr != 0x46) {
		app_log_info("Invalid address 0x%x for SHT4x device" APP_LOG_NL, addr);
		return SL_STATUS_INVALID_PARAMETER;
	}
	self->address        = addr;
	self->command_buffer = 0;
	self->callback.ptr   = NULL;

	result = sht4x_read_serial_number_blocking(self);
	if (result.status == SL_STATUS_OK) {
		app_log_info(
		    "found SHT4x device at %s.0x%x: %lx" APP_LOG_NL,
		    i2c_schd_get_instance_name(i2c),
		    addr,
		    result.data.serial_number
		);
		return SL_STATUS_OK;
	}

	app_log_warning(
	    "No SHT4x device at %s.0x%x, retrying in 80ms" APP_LOG_NL,
	    i2c_schd_get_instance_name(i2c),
	    addr
	);
	sl_sleeptimer_delay_millisecond(80);

	result = sht4x_read_serial_number_blocking(self);
	if (result.status != SL_STATUS_OK) {
		app_log_error(
		    "No SHT4x device found at %s.0x%x" APP_LOG_NL,
		    i2c_schd_get_instance_name(i2c),
		    addr
		);
		return SL_STATUS_INITIALIZATION;
	}

	app_log_info(
	    "found SHT4x device at %s.0x%x SN:%lx" APP_LOG_NL,
	    i2c_schd_get_instance_name(i2c),
	    addr,
	    result.data.serial_number
	);
	return SL_STATUS_OK;
}

/// Marks the on-going command as having an error.
void _sht4x_error_cmd(sht4x_handle_t *self, sl_status_t status) {
	uint8_t          command;
	sht4x_callback_u callback;
	void            *user_data;
	CORE_ATOMIC_SECTION({
		command              = self->command_buffer;
		callback             = self->callback;
		user_data            = self->user_data;
		self->command_buffer = 0;
		self->callback.ptr   = NULL;
		self->user_data      = NULL;
	});

	if (callback.ptr == NULL) {
		app_log_error("NULL callback in SHT4x driver async error" APP_LOG_NL);
		return;
	}

	switch (command) {
	case SHT4X_READ_SERIAL_NUMBER:
		callback.serial_number(status, -1, user_data);
		break;
	case SHT4X_SOFT_RESET:
		callback.soft_reset(status, user_data);
		break;
	default:
		callback.data(status, 0xffff, 0xffff, user_data);
	}
}

/// Marks the on-going command as completed succesfully.
void _sht4x_complete_cmd(sht4x_handle_t *self, sht4x_blocking_result_t *res) {
	uint8_t          command;
	sht4x_callback_u callback;
	void            *user_data;
	CORE_ATOMIC_SECTION({
		command              = self->command_buffer;
		callback             = self->callback;
		user_data            = self->user_data;
		self->command_buffer = 0;
		self->callback.ptr   = NULL;
		self->user_data      = NULL;
	});

	if (callback.ptr == NULL) {
		app_log_error("NULL callback in SHT4x driver async complete" APP_LOG_NL
		);
		return;
	}

	switch (command) {
	case SHT4X_READ_SERIAL_NUMBER:
		callback.serial_number(res->status, res->data.serial_number, user_data);
		break;
	case SHT4X_SOFT_RESET:
		callback.soft_reset(res->status, user_data);
		break;
	default:
		callback.data(
		    res->status,
		    res->data.th_readout.temperature,
		    res->data.th_readout.humidity,
		    user_data
		);
	}
}

bool _sht4x_check_crc(const uint8_t *buffer, size_t len) {
	uint8_t crc = 0xff;
	for (size_t i = 0; i < len; ++i) {
		crc = CRC8_AppendByte(crc, 0x31, buffer[i]);
	}
	return crc == 0x00;
}

temperature_t _sht4x_convert_temperature(uint16_t raw) {
	return (int16_t)((temperature_t)((((float)raw) * 17500.0f) / 65535.0f -
	                                 4500.0f));
}

humidity_t _sht4x_convert_humidity(uint16_t raw) {
	return (humidity_t)((((float)raw) * 1000.0f) / 65535.0f);
}

void _sht4x_parse_data(sht4x_handle_t *self, sht4x_blocking_result_t *res) {
	bool dataAOk = _sht4x_check_crc(&self->read_buffer[0], 3);
	bool dataBOk = _sht4x_check_crc(&self->read_buffer[3], 3);
	if (dataAOk && dataBOk) {
		res->status = SL_STATUS_OK;
	} else {
		res->status = SL_STATUS_TRANSMIT_INCOMPLETE;
	}
	switch (self->command_buffer) {
	case SHT4X_SOFT_RESET:
		break;
	case SHT4X_READ_SERIAL_NUMBER:
		if (res->status == SL_STATUS_OK) {
			res->data.serial_number = ((uint32_t)self->read_buffer[0] << 24) |
			                          ((uint32_t)self->read_buffer[1] << 16) |
			                          ((uint32_t)self->read_buffer[3] << 8) |
			                          ((uint32_t)self->read_buffer[4] << 0);
		} else {
			res->data.serial_number = -1;
		}
		break;
	default:
		if (dataAOk) {
			res->data.th_readout.temperature = _sht4x_convert_temperature(
			    ((uint16_t)self->read_buffer[0] << 8) |
			    ((uint16_t)self->read_buffer[1])
			);
		} else {
			res->data.th_readout.temperature = 0xffff;
		}
		if (dataBOk) {
			res->data.th_readout.humidity = _sht4x_convert_humidity(
			    ((uint16_t)self->read_buffer[3] << 8) |
			    ((uint16_t)self->read_buffer[4])
			);
		} else {
			res->data.th_readout.humidity = 0xffff;
		}
	}
}

// callback when receiving the command readout.
void _sht4x_on_i2c_read_complete(i2c_tx_status_t status, void *user_data) {
	sht4x_handle_t         *self = user_data;
	sht4x_blocking_result_t res;
	res.status = i2c_tx_status_map(status);
	if (status != I2C_TX_OK) {
		_sht4x_error_cmd(self, res.status);
		return;
	}
	_sht4x_parse_data(self, &res);
	// note parse data may have failed the error for invalid (partial) data.
	_sht4x_complete_cmd(self, &res);
};

// timeout function on the sleep timer to trigger the RX of data (or mark
// completion).
void _sht4x_on_timer_timeout(
    sl_sleeptimer_timer_handle_t *handle, void *user_data
) {
	(void)handle;
	sht4x_handle_t  *self = user_data;
	uint8_t          command;
	sht4x_callback_u callback;

	CORE_ATOMIC_SECTION({ command = self->command_buffer; });

	if (command == SHT4X_SOFT_RESET) {
		CORE_ATOMIC_SECTION({
			callback             = self->callback;
			user_data            = self->user_data;
			self->command_buffer = 0;
			self->callback.ptr   = NULL;
			self->user_data      = NULL;
		});
		callback.soft_reset(SL_STATUS_OK, user_data);
		return;
	}

	sl_status_t status = i2c_schd_receive(
	    self->i2c_bus,
	    self->address,
	    self->read_buffer,
	    6,
	    &_sht4x_on_i2c_read_complete,
	    self
	);

	if (status != SL_STATUS_OK) {
		_sht4x_error_cmd(self, status);
	}
}

// Callback on the write TX that starts a timeout.
void _sht4x_on_i2c_write_complete(i2c_tx_status_t status, void *user_data) {
	sht4x_handle_t *self = user_data;
	if (status != I2C_TX_OK) {
		app_log_debug(
		    "Could not write command bus error: 0x%04X" APP_LOG_NL,
		    status
		);
		_sht4x_error_cmd(
		    self,
		    status == I2C_TX_FOLLOWER_ACK_ERROR ? SL_STATUS_NOT_FOUND
		                                        : SL_STATUS_BUS_ERROR
		);
		return;
	}

	sl_sleeptimer_start_timer_ms(
	    &self->timer,
	    self->read_delay_ms,
	    &_sht4x_on_timer_timeout,
	    self,
	    0,
	    0
	);
};

// checks if it is a valid command.
sl_status_t _sht4x_check_command(uint8_t command) {
	switch (command) {
	case SHT4X_SOFT_RESET:
	case SHT4X_READ_SERIAL_NUMBER:
	case SHT4X_MEASURE_HIGH_P:
	case SHT4X_MEASURE_MEDIUM_P:
	case SHT4X_MEASURE_LOW_P:
		return SL_STATUS_OK;
	default:
		return SL_STATUS_INVALID_PARAMETER;
	}
}

// checks if it is a valid command for reading values.
sl_status_t _sht4x_check_readout_command(uint8_t command) {
	switch (command) {
	case SHT4X_MEASURE_HIGH_P:
	case SHT4X_MEASURE_MEDIUM_P:
	case SHT4X_MEASURE_LOW_P:
		return SL_STATUS_OK;
	default:
		return SL_STATUS_INVALID_PARAMETER;
	}
}

// sends a command to the device, asynchronously.
sl_status_t _sht4x_send_command(
    sht4x_handle_t *self,
    sht4x_command_e command,
    uint8_t         read_delay_ms,
    void           *callback,
    void           *user_data
) {
	if (callback == NULL) {
		return SL_STATUS_NULL_POINTER;
	}

	CORE_DECLARE_IRQ_STATE;
	CORE_ENTER_ATOMIC();
	if (self->command_buffer != 0) {
		CORE_EXIT_ATOMIC();
		return SL_STATUS_BUSY;
	}
	self->command_buffer = command;
	self->read_delay_ms  = read_delay_ms;
	self->callback.ptr   = callback;
	self->user_data      = user_data;
	CORE_EXIT_ATOMIC();

	sl_status_t status = i2c_schd_send(
	    self->i2c_bus,
	    self->address,
	    &self->command_buffer,
	    1,
	    &_sht4x_on_i2c_write_complete,
	    self
	);
	if (status != SL_STATUS_OK) {
		app_log_debug("Could not write command: 0x%04lX" APP_LOG_NL, status);
		CORE_ENTER_ATOMIC();
		self->command_buffer = 0;
		self->callback.ptr   = NULL;
		self->user_data      = NULL;
		CORE_EXIT_ATOMIC();
	}

	return status;
}

// sends a command, blocking fashion.
sht4x_blocking_result_t sht4x_send_command_blocking(
    sht4x_handle_t *self, sht4x_command_e command, uint8_t read_delay_ms
) {
	sht4x_blocking_result_t res;

	self->command_buffer = command;
	res.status           = i2c_schd_send_blocking(
        self->i2c_bus,
        self->address,
        &self->command_buffer,
        1
    );
	if (res.status != SL_STATUS_OK) {
		self->command_buffer = 0;
		return res;
	}
	sl_sleeptimer_delay_millisecond(read_delay_ms);
	res.status = i2c_schd_receive_blocking(
	    self->i2c_bus,
	    self->address,
	    &self->read_buffer[0],
	    6
	);
	if (res.status != SL_STATUS_OK) {
		self->command_buffer = 0;
		return res;
	}
	_sht4x_parse_data(self, &res);
	self->command_buffer = 0;
	return res;
}

sl_status_t sht4x_read_serial_number(
    sht4x_handle_t                     *self,
    sht4x_read_serial_number_callback_t cb,
    void                               *user_data
) {
	return _sht4x_send_command(
	    self,
	    SHT4X_READ_SERIAL_NUMBER,
	    1,
	    (void *)cb,
	    user_data
	);
}

sht4x_blocking_result_t sht4x_read_serial_number_blocking(sht4x_handle_t *self
) {
	return sht4x_send_command_blocking(self, SHT4X_READ_SERIAL_NUMBER, 1);
}

sl_status_t sht4x_read_data(
    sht4x_handle_t            *self,
    sht4x_command_e            type,
    sht4x_read_data_callback_t callback,
    void                      *user_data
) {

	sl_status_t status = _sht4x_check_readout_command(type);
	if (status != SL_STATUS_OK) {
		return status;
	}

	return _sht4x_send_command(self, type, 10, (void *)callback, user_data);
}

sht4x_blocking_result_t
sht4x_read_data_blocking(sht4x_handle_t *self, sht4x_command_e cmd) {
	sht4x_blocking_result_t res;
	res.status = _sht4x_check_readout_command(cmd);
	if (res.status != SL_STATUS_OK) {
		return res;
	}
	return sht4x_send_command_blocking(self, cmd, 10);
}

sl_status_t sht4x_soft_reset(
    sht4x_handle_t *self, sht4x_soft_reset_callback_t cb, void *user_data
) {
	return _sht4x_send_command(
	    self,
	    SHT4X_SOFT_RESET,
	    80,
	    (void *)cb,
	    user_data
	);
}

sl_status_t sht4x_soft_reset_blocking(sht4x_handle_t *self) {
	sht4x_blocking_result_t res =
	    sht4x_send_command_blocking(self, SHT4X_SOFT_RESET, 80);
	return res.status;
}
