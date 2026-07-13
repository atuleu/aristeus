#pragma once

#include "sl_status.h"
#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

/// forward declaration
typedef struct sl_i2c sl_i2c_t;

/// Base address for SHT4x devices.
#define SHT4X_BASE_ADDR 0x44

/// A callback for all kinds of command for SHT4X devices. Could be internal.
typedef void (*app_sht4x_send_command_callback_t)(
    uint16_t dataA, uint16_t dataB
);

/// List of commands for SHT4X.
typedef enum app_sht4x_command {
	SHT4X_MEASURE_HIGH_P     = 0xFD,
	SHT4X_MEASURE_MEDIUM_P   = 0xF6,
	SHT4X_MEASURE_LOW_P      = 0xE0,
	SHT4X_READ_SERIAL_NUMBER = 0x89,
	SHT4X_SOFT_RESET         = 0x94,
	// missing commands with active heater deliberatly.

} app_sht4x_command_e;

/// Inits the SHT4X readouts.
sl_status_t app_sht4x_init(sl_i2c_t *i2c, uint8_t addr);

/// Sends a command to the SHT4X device.
sl_status_t app_sht4x_send_command(
    app_sht4x_command_e command, app_sht4x_send_command_callback_t callback
);

/// A callback for reading out the serial number.
typedef void (*app_sht4x_read_serial_number_callback_t)(
    sl_status_t status, uint32_t serial
);

/// Asynchronously read the serial number of the chip.
sl_status_t
app_sht4x_read_serial_number(app_sht4x_read_serial_number_callback_t o);

/// read temperature and humidity callback.
typedef void (*app_sht4x_read_temperature_callback_t)(
    sl_status_t status, temperature_t temperature, humidity_t humidity
);

/// Asynchronously read temperature.
sl_status_t app_sht4x_read_temperature(
    app_sht4x_command_e type, app_sht4x_read_temperature_callback_t callback
);

/// Result for synchronous polling call.
typedef struct {
	sl_status_t status;

	union {
		uint32_t serial_number;

		struct __attribute__((packed)) {
			temperature_t temperature;
			humidity_t    humidity;
		} th_readout;

		struct __attribute__((packed)) {
			uint16_t dataA;
			uint16_t dataB;
		} data;
	} data;
} app_sht4x_blocking_result_t;

/// Synchronously read the serial number.
app_sht4x_blocking_result_t app_sht4x_read_serial_number_blocking();

/// Synchronously read a temperature.
app_sht4x_blocking_result_t
app_sht4x_read_temperature_blocking(app_sht4x_command_e command);

#ifdef __cplusplus
}
#endif
