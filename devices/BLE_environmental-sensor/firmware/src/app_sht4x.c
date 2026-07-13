#include "app_sht4x.h"
#include "app_i2c0_claim.h"
#include "app_log.h"
#include "crc8.h"
#include "sl_device_peripheral.h"
#include "sl_i2c.h"
#include "sl_sleeptimer.h"

#include "sl_status.h"
#include "types.h"
#include <stdint.h>

const char *get_i2c_instance_name(sl_i2c_handle_t *i2c) {
	if (i2c == NULL) {
		return "I2C<NULL>";
	}
	if (i2c->i2c_peripheral == SL_PERIPHERAL_I2C0) {
		return "I2C0";
	}
	if (i2c->i2c_peripheral == SL_PERIPHERAL_I2C1) {
		return "I2C1";
	}
	if (i2c->i2c_peripheral == SL_PERIPHERAL_I2C2) {
		return "I2C2";
	}
	if (i2c->i2c_peripheral == SL_PERIPHERAL_I2C3) {
		return "I2C3";
	}
	return "I2C<Unknown>";
}

sl_status_t
app_sht4x_init(app_sht4x_handle_t *self, sl_i2c_handle_t *i2c, uint8_t addr) {
	app_sht4x_blocking_result_t result;
	self->i2c_bus = i2c;
	if (addr != 0x44 && addr != 0x45 && addr != 0x46) {
		app_log_info("Invalid address 0x%x for SHT4x device" APP_LOG_NL, addr);
		return SL_STATUS_INVALID_PARAMETER;
	}
	// TODO: Address check ??
	self->address        = addr;
	self->command_buffer = 0;
	self->callback.ptr   = NULL;

	result = app_sht4x_read_serial_number_blocking(self);
	if (result.status == SL_STATUS_OK) {
		app_log_info(
		    "found SHT4x device at %s.0x%x: %lux" APP_LOG_NL,
		    get_i2c_instance_name(i2c),
		    addr,
		    result.data.serial_number
		);
		return SL_STATUS_OK;
	}

	app_log_warning(
	    "No SHT4x device at %s.0x%x, retrying in 80ms" APP_LOG_NL,
	    get_i2c_instance_name(i2c),
	    addr
	);
	sl_sleeptimer_delay_millisecond(80);

	result = app_sht4x_read_serial_number_blocking(self);
	if (result.status != SL_STATUS_OK) {
		app_log_error(
		    "No SHT4x device found at %s.0x%x" APP_LOG_NL,
		    get_i2c_instance_name(i2c),
		    addr
		);
		return SL_STATUS_INITIALIZATION;
	}

	app_log_info(
	    "found SHT4x device at %s.0x%x SN:%lux" APP_LOG_NL,
	    get_i2c_instance_name(i2c),
	    addr,
	    result.data.serial_number
	);
	return SL_STATUS_OK;
}

void app_sht4x_bus_cleanup(app_sht4x_handle_t *self) {
	self->command_buffer = 0;
	self->callback.ptr   = NULL;
	sl_i2c_set_transfer_complete_callback(self->i2c_bus, NULL);
	sl_i2c_set_event_callback(self->i2c_bus, NULL);
	app_i2c_unclaim(self->i2c_bus);
}

/// Marks the on-going command as having an error.
void app_sht4x_error_cmd(app_sht4x_handle_t *self, sl_status_t status) {
	uint8_t              command  = self->command_buffer;
	app_sht4x_callback_u callback = self->callback;
	app_sht4x_bus_cleanup(self);

	if (callback.ptr == NULL) {
		app_log_error("NULL callback in SHT4x driver async error" APP_LOG_NL);
		return;
	}

	switch (command) {
	case SHT4X_READ_SERIAL_NUMBER:
		callback.serial_number(status, -1);
		break;
	case SHT4X_SOFT_RESET:
		callback.soft_reset(status);
		break;
	default:
		callback.data(status, 0xffff, 0xffff);
	}
}

/// Marks the on-going command as completed succesfully.
void app_sht4x_complete_cmd(
    app_sht4x_handle_t *self, app_sht4x_blocking_result_t *res
) {
	uint8_t              command  = self->command_buffer;
	app_sht4x_callback_u callback = self->callback;

	app_sht4x_bus_cleanup(self);
	self->callback.ptr = NULL;

	if (callback.ptr == NULL) {
		app_log_error("NULL callback in SHT4x driver async complete" APP_LOG_NL
		);
		return;
	}

	switch (command) {
	case SHT4X_READ_SERIAL_NUMBER:
		callback.serial_number(res->status, res->data.serial_number);
		break;
	case SHT4X_SOFT_RESET:
		callback.soft_reset(res->status);
		break;
	default:
		callback.data(
		    res->status,
		    res->data.th_readout.temperature,
		    res->data.th_readout.humidity
		);
	}
}

sl_status_t app_sht4x_on_i2c_event(
    sl_i2c_handle_t *i2c, sl_i2c_event_t e, void *user_data
) {
	(void)i2c;
	app_sht4x_handle_t *self = user_data;

	if (e == SL_I2C_EVENT_IN_PROGRESS || e == SL_I2C_EVENT_COMPLETED ||
	    e == SL_I2C_EVENT_IDLE) {
		return SL_STATUS_OK;
	}
	app_sht4x_error_cmd(
	    self,
	    e == SL_I2C_EVENT_ADDR_NACK ? SL_STATUS_NOT_FOUND : SL_STATUS_BUS_ERROR
	);
	return SL_STATUS_OK;
}

bool app_sht4x_check_crc(const uint8_t *buffer, size_t len) {
	uint8_t crc = 0xff;
	for (size_t i = 0; i < len; ++i) {
		crc = CRC8_AppendByte(crc, 0x31, buffer[i]);
	}
	return crc == 0x00;
}

temperature_t app_sht4x_convert_temperature(uint16_t raw) {
	return (int16_t)((temperature_t)((((float)raw) * 17500.0f) / 65535.0f -
	                                 4500.0f));
}

humidity_t app_sht4x_convert_humidity(uint16_t raw) {
	return (humidity_t)((((float)raw) * 1000.0f) / 65535.0f);
}

void app_sht4x_parse_data(
    app_sht4x_handle_t *self, app_sht4x_blocking_result_t *res
) {
	bool dataAOk = app_sht4x_check_crc(&self->read_buffer[0], 3);
	bool dataBOk = app_sht4x_check_crc(&self->read_buffer[3], 3);
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
			res->data.th_readout.temperature = app_sht4x_convert_temperature(
			    ((uint16_t)self->read_buffer[0] << 8) |
			    ((uint16_t)self->read_buffer[1])
			);
		} else {
			res->data.th_readout.temperature = 0xffff;
		}
		if (dataBOk) {
			res->data.th_readout.humidity = app_sht4x_convert_humidity(
			    ((uint16_t)self->read_buffer[3] << 8) |
			    ((uint16_t)self->read_buffer[4])
			);
		} else {
			res->data.th_readout.humidity = 0xffff;
		}
	}
}

// callback when receiving the command readout.
sl_status_t
app_sht4x_on_i2c_read_complete(sl_i2c_handle_t *i2c_handle, void *user_data) {
	(void)i2c_handle;
	app_sht4x_handle_t         *self = user_data;
	app_sht4x_blocking_result_t res;

	app_sht4x_parse_data(self, &res);
	// note parse data may have failed the error for invalid (partial) data.
	app_sht4x_complete_cmd(self, &res);
	return SL_STATUS_OK;
};

// timeout function on the sleep timer to trigger the RX of data (or mark
// completion).
void app_sht4x_on_timer_timeout(
    sl_sleeptimer_timer_handle_t *handle, void *user_data
) {
	(void)handle;
	app_sht4x_handle_t *self = user_data;

	if (self->command_buffer == SHT4X_SOFT_RESET) {
		app_sht4x_callback_u callback = self->callback;
		app_sht4x_bus_cleanup(self);
		callback.soft_reset(SL_STATUS_OK);
		return;
	}
	sl_status_t status = sl_i2c_set_transfer_complete_callback(
	    self->i2c_bus,
	    &app_sht4x_on_i2c_read_complete
	);
	if (status != SL_STATUS_OK) {
		app_sht4x_error_cmd(self, status);
	}

	status = sl_i2c_leader_receive_non_blocking(
	    self->i2c_bus,
	    self->address,
	    self->read_buffer,
	    6,
	    self
	);

	if (status != SL_STATUS_OK) {
		app_sht4x_error_cmd(self, status);
	}
}

// Callback on the write TX that starts a timeout.
sl_status_t
app_sht4x_on_i2c_write_complete(sl_i2c_handle_t *i2c, void *user_data) {
	(void)i2c;
	app_sht4x_handle_t *self = user_data;
	// here always succesful.
	sl_sleeptimer_start_timer_ms(
	    &self->timer,
	    self->read_delay_ms,
	    &app_sht4x_on_timer_timeout,
	    self,
	    0,
	    0
	);
	return SL_STATUS_OK;
};

// checks if it is a valid command.
sl_status_t app_sht4x_check_command(uint8_t command) {
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
sl_status_t app_sht4x_check_readout_command(uint8_t command) {
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
sl_status_t app_sht4x_send_command(
    app_sht4x_handle_t *self,
    app_sht4x_command_e command,
    uint8_t             read_delay_ms,
    void               *callback
) {
	if (callback == NULL) {
		return SL_STATUS_NULL_POINTER;
	}

	sl_status_t status = app_i2c_claim(self->i2c_bus);

	if (status != SL_STATUS_OK) {
		return status;
	}

	status = sl_i2c_set_transfer_complete_callback(
	    self->i2c_bus,
	    &app_sht4x_on_i2c_write_complete
	);
	if (status != SL_STATUS_OK) {
		app_sht4x_bus_cleanup(self);
		return status;
	}

	status = sl_i2c_set_event_callback(self->i2c_bus, &app_sht4x_on_i2c_event);
	if (status != SL_STATUS_OK) {
		app_sht4x_bus_cleanup(self);
		return status;
	}

	self->command_buffer = command;
	self->read_delay_ms  = read_delay_ms;
	self->callback.ptr   = callback;
	status               = sl_i2c_leader_send_non_blocking(
        self->i2c_bus,
        self->address,
        &self->command_buffer,
        1,
        (void *)self
    );
	if (status != SL_STATUS_OK) {
		app_sht4x_bus_cleanup(self);
		self->callback.ptr = NULL;
	}

	return status;
}

// sends a command, blocking fashion.
app_sht4x_blocking_result_t app_sht4x_send_command_blocking(
    app_sht4x_handle_t *self, app_sht4x_command_e command, uint8_t read_delay_ms
) {
	app_sht4x_blocking_result_t res;
	res.status = app_i2c_claim(self->i2c_bus);
	if (res.status != SL_STATUS_OK) {
		return res;
	}

	self->command_buffer = command;
	res.status           = sl_i2c_leader_send_blocking(
        self->i2c_bus,
        self->address,
        &self->command_buffer,
        1,
        5
    );
	if (res.status != SL_STATUS_OK) {
		app_sht4x_bus_cleanup(self);
		return res;
	}
	sl_sleeptimer_delay_millisecond(read_delay_ms);
	res.status = sl_i2c_leader_receive_blocking(
	    self->i2c_bus,
	    self->address,
	    &self->read_buffer[0],
	    6,
	    5
	);
	if (res.status != SL_STATUS_OK) {
		app_sht4x_bus_cleanup(self);
		return res;
	}
	app_sht4x_parse_data(self, &res);
	app_sht4x_bus_cleanup(self);
	return res;
}

sl_status_t app_sht4x_read_serial_number(
    app_sht4x_handle_t *self, app_sht4x_read_serial_number_callback_t cb
) {
	return app_sht4x_send_command(
	    self,
	    SHT4X_READ_SERIAL_NUMBER,
	    1,
	    (void *)cb
	);
}

app_sht4x_blocking_result_t
app_sht4x_read_serial_number_blocking(app_sht4x_handle_t *self) {
	return app_sht4x_send_command_blocking(self, SHT4X_READ_SERIAL_NUMBER, 1);
}

sl_status_t app_sht4x_read_data(
    app_sht4x_handle_t            *self,
    app_sht4x_command_e            type,
    app_sht4x_read_data_callback_t callback
) {

	sl_status_t status = app_sht4x_check_readout_command(type);
	if (status != SL_STATUS_OK) {
		return status;
	}

	return app_sht4x_send_command(self, type, 10, (void *)callback);
}

app_sht4x_blocking_result_t app_sht4x_read_data_blocking(
    app_sht4x_handle_t *self, app_sht4x_command_e cmd
) {
	app_sht4x_blocking_result_t res;
	res.status = app_sht4x_check_readout_command(cmd);
	if (res.status != SL_STATUS_OK) {
		return res;
	}
	return app_sht4x_send_command_blocking(self, cmd, 10);
}

sl_status_t app_sht4x_soft_reset(
    app_sht4x_handle_t *self, app_sht4x_soft_reset_callback_t cb
) {
	return app_sht4x_send_command(self, SHT4X_SOFT_RESET, 80, (void *)cb);
}

sl_status_t app_sht4x_soft_reset_blocking(app_sht4x_handle_t *self) {
	app_sht4x_blocking_result_t res =
	    app_sht4x_send_command_blocking(self, SHT4X_SOFT_RESET, 80);
	return res.status;
}
