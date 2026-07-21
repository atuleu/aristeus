#pragma once

#include "drivers/i2c_schd.h"
#include "sl_sleeptimer.h"
#include "sl_status.h"
#include "types.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

/// forward declaration for an handle. Please treat is as an opaque type.
typedef struct sht4x_handle sht4x_handle_t;

/// Base address for SHT4x devices.
#define SHT4X_BASE_ADDR 0x44

/// List of commands for SHT4X.
SL_ENUM(sht4x_command_e){
    SHT4X_MEASURE_HIGH_P     = 0xFD,
    SHT4X_MEASURE_MEDIUM_P   = 0xF6,
    SHT4X_MEASURE_LOW_P      = 0xE0,
    SHT4X_READ_SERIAL_NUMBER = 0x89,
    SHT4X_SOFT_RESET         = 0x94,
    // missing commands with active heater deliberatly.

};

/**
 * Initializes the SHT4x sensor handle.
 *
 * @param self Pointer to the SHT4x handle to initialize.
 * @param i2c Pointer to the I2C handle used for communication.
 * @param addr I2C address of the SHT4x sensor (0x44 or 0x45).
 *
 * @return SL_STATUS_OK if successful, error code otherwise.
 */
sl_status_t
sht4x_init(sht4x_handle_t *self, i2c_schd_handle_t *i2c, uint8_t addr);

/**
 * Asynchronous serial number read callback
 * @param status Status of the read operation.
 * @param serial Serial number read from the sensor. is 0xFFFFFFFF if the read
 * failed.
 */
typedef void (*sht4x_read_serial_number_callback_t)(
    sl_status_t status, uint32_t serial, void *user_data
);

/**
 * Asynchronously read the serial number of the SHT4x sensor.
 *
 * @param self Pointer to the SHT4x handle.
 * @param cb Callback function to be called upon completion of the read
 *        operation.
 *
 * @return SL_STATUS_OK if the read operation was initiated successfully, error
 *         code otherwise.
 */
sl_status_t sht4x_read_serial_number(
    sht4x_handle_t                     *self,
    sht4x_read_serial_number_callback_t cb,
    void                               *user_data
);

/**
 * Asynchronous read data callback
 *
 * @param status Status of the read operation. SL_STATUS_OK if the read
 *        operation was successful, and both CRC checks passed. Otherwise, an
 *        error code indicating the failure reason.
 * @param temperature Temperature read from the sensor. is 0xFFFF if read or CRC
 *        check failed.
 * @param humidity Humidity read from the sensor. is 0xFFFF if read or CRC check
 *        failed.
 *
 *
 */
typedef void (*sht4x_read_data_callback_t)(
    sl_status_t   status,
    temperature_t temperature,
    humidity_t    humidity,
    void         *user_data
);

/**
 * Asynchronously read temperature and humidity data from the SHT4x sensor.
 *
 * @param self Pointer to the SHT4x handle.
 * @param type Command type for the read operation (e.g., SHT4X_MEASURE_HIGH_P).
 * @param callback Callback function to be called upon completion of the read
 *
 * @return
 * - SL_STATUS_OK if the read operation was initiated successfully.
 * - SL_STATUS_INVALID_PARAMETER if the command type is invalid.
 * - other error codes indicating failure to initiate the read operation.
 */
sl_status_t sht4x_read_data(
    sht4x_handle_t            *self,
    sht4x_command_e            type,
    sht4x_read_data_callback_t callback,
    void                      *user_data
);

/**
 * Asynchronous soft reset callback
 *
 * @param status Status of the soft reset operation. SL_STATUS_OK if the soft
 *        reset operation was successful. Otherwise, an error code indicating
 *        the failure reason.
 */
typedef void (*sht4x_soft_reset_callback_t)(
    sl_status_t status, void *user_data
);

/**
 * Asynchronously perform a soft reset on the SHT4x sensor.
 *
 * @param self Pointer to the SHT4x handle.
 * @param cb Callback function to be called upon completion of the soft reset
 *        operation.
 *
 * @return SL_STATUS_OK if the soft reset operation was initiated successfully,
 *         error code otherwise.
 */
sl_status_t sht4x_soft_reset(
    sht4x_handle_t *self, sht4x_soft_reset_callback_t cb, void *user_data
);

/**
 * Synchronously perform a soft reset on the SHT4x sensor.
 *
 * @note BLOCKING function!
 *
 * @param self Pointer to the SHT4x handle.
 *
 * @return SL_STATUS_OK if the soft reset operation was successful, error code
 *         otherwise.
 */
sl_status_t sht4x_soft_reset_blocking(sht4x_handle_t *self);

/**
 * Result structure for blocking operations.
 */
typedef struct {
	/// Status of the operation. SL_STATUS_OK if the operation was successful,
	/// error code otherwise.
	sl_status_t status;

	/// Data returned by the operation. The interpretation of this data depends
	/// on the specific operation performed. For example, it may contain the
	/// serial number, temperature, or humidity read from the sensor.
	union {
		/// The serial number read from the sensor. Valid only if the operation
		/// was sht4x_read_serial_number_blocking. If the operation failed, this
		/// value is 0xffffffff.
		uint32_t serial_number;

		/// Temperature and humidity read from the sensor. Valid only if the
		/// operation was sht4x_read_data_blocking. If the operation failed,
		/// these values are 0xffff.
		struct __attribute__((packed)) {
			temperature_t temperature;
			humidity_t    humidity;
		} th_readout;

	} data;
} sht4x_blocking_result_t;

/**
 * Sends a command to the SHT4x sensor and waits for the result.
 *
 * @param instance Pointer to the SHT4x handle.
 * @param command The command to send to the sensor.
 * @param read_delay_ms The delay in milliseconds to wait before reading the
 *        result after sending the command.
 *
 * @return A sht4x_blocking_result_t structure containing the status of the
 *         operation and any data returned by the sensor.
 */
sht4x_blocking_result_t sht4x_send_command_blocking(
    sht4x_handle_t *instance, sht4x_command_e command, uint8_t read_delay_ms
);

/**
 * Synchronously read the serial number from the SHT4x sensor.
 *
 * @param instance Pointer to the SHT4x handle.
 *
 * @return A sht4x_blocking_result_t structure containing the status of the
 *         operation and the serial number read from the sensor. If the
 *         operation failed, the serial number will be 0xffffffff.
 */
sht4x_blocking_result_t sht4x_read_serial_number_blocking(sht4x_handle_t *self);

/**
 * Synchronously read the temperature and humidity data from the SHT4x sensor.
 *
 * @param instance Pointer to the SHT4x handle.
 *
 * @return A sht4x_blocking_result_t structure containing the status of the
 *         operation and the temperature and humidity read from the sensor. If
 *         the operation failed, the temperature and humidity will be 0xffff.
 */
sht4x_blocking_result_t
sht4x_read_data_blocking(sht4x_handle_t *self, sht4x_command_e command);

/**
 * Union to hold different types of callbacks for SHT4x operations. This allows
 * for a single callback pointer to be used for different operations, with the
 * appropriate callback type being selected based on the operation being
 * performed.
 */
typedef union {
	sht4x_read_serial_number_callback_t serial_number;
	sht4x_read_data_callback_t          data;
	sht4x_soft_reset_callback_t         soft_reset;
	void                               *ptr;
} sht4x_callback_u;

/**
 * Private SHT4x sensor handle structure. Treat this as an opaque type; do not
 * access its members directly. Use the provided API functions to interact with
 * the SHT4x sensor.
 */
struct sht4x_handle {
	i2c_schd_handle_t           *i2c_bus;
	uint8_t                      address;
	uint8_t                      read_delay_ms;
	uint8_t                      command_buffer;
	uint8_t                      read_buffer[6];
	sl_sleeptimer_timer_handle_t timer;

	sht4x_callback_u callback;
	void            *user_data;
};

#ifdef __cplusplus
}
#endif
