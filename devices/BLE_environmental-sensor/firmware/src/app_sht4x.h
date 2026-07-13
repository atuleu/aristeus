#pragma once

#include "sl_i2c.h"
#include "sl_sleeptimer.h"
#include "sl_status.h"
#include "types.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// forward declaration for an handle. Please treat is as an opaque type.
typedef struct app_sht4x_handle app_sht4x_handle_t;

/// Base address for SHT4x devices.
#define SHT4X_BASE_ADDR 0x44

/// List of commands for SHT4X.
typedef enum app_sht4x_command {
	SHT4X_MEASURE_HIGH_P     = 0xFD,
	SHT4X_MEASURE_MEDIUM_P   = 0xF6,
	SHT4X_MEASURE_LOW_P      = 0xE0,
	SHT4X_READ_SERIAL_NUMBER = 0x89,
	SHT4X_SOFT_RESET         = 0x94,
	// missing commands with active heater deliberatly.

} app_sht4x_command_e;

/// Inits the SHT4X instance.
sl_status_t
app_sht4x_init(app_sht4x_handle_t *self, sl_i2c_handle_t *i2c, uint8_t addr);

/// A callback for reading out the serial number.
typedef void (*app_sht4x_read_serial_number_callback_t)(
    sl_status_t status, uint32_t serial
);

/// Asynchronously read the serial number of the chip.
sl_status_t app_sht4x_read_serial_number(
    app_sht4x_handle_t *self, app_sht4x_read_serial_number_callback_t cb
);

/// read temperature and humidity callback.
typedef void (*app_sht4x_read_data_callback_t)(
    sl_status_t status, temperature_t temperature, humidity_t humidity
);

/// Asynchronously read temperature.
sl_status_t app_sht4x_read_data(
    app_sht4x_handle_t            *self,
    app_sht4x_command_e            type,
    app_sht4x_read_data_callback_t callback
);

typedef void (*app_sht4x_soft_reset_callback_t)(sl_status_t);

sl_status_t app_sht4x_soft_reset(
    app_sht4x_handle_t *inst, app_sht4x_soft_reset_callback_t cb
);

sl_status_t app_sht4x_soft_reset_blocking(app_sht4x_handle_t *inst);

/// Result for synchronous polling call.
typedef struct {
	sl_status_t status;

	union {
		uint32_t serial_number;

		struct __attribute__((packed)) {
			temperature_t temperature;
			humidity_t    humidity;
		} th_readout;

	} data;
} app_sht4x_blocking_result_t;

/// Sends a command synchronously.
app_sht4x_blocking_result_t app_sht4x_send_command_blocking(
    app_sht4x_handle_t *instance,
    app_sht4x_command_e command,
    uint8_t             read_delay_ms
);

/// Synchronously read the serial number.
app_sht4x_blocking_result_t
app_sht4x_read_serial_number_blocking(app_sht4x_handle_t *self);

/// Synchronously read a temperature.
app_sht4x_blocking_result_t app_sht4x_read_data_blocking(
    app_sht4x_handle_t *self, app_sht4x_command_e command
);

/// Any types of supported callback.
typedef union {
	app_sht4x_read_serial_number_callback_t serial_number;
	app_sht4x_read_data_callback_t          data;
	app_sht4x_soft_reset_callback_t         soft_reset;
	void                                   *ptr;
} app_sht4x_callback_u;

/// data for each instance. Please treat it as opaque. It is open to allow
/// static optimizations.
struct app_sht4x_handle {
	sl_i2c_handle_t             *i2c_bus;
	uint8_t                      address;
	uint8_t                      read_delay_ms;
	uint8_t                      command_buffer;
	uint8_t                      read_buffer[6];
	sl_sleeptimer_timer_handle_t timer;

	app_sht4x_callback_u callback;
};

#ifdef __cplusplus
}
#endif
