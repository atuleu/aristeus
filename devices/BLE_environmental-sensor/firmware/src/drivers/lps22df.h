#pragma once

#include "sl_device_gpio.h"
#include "sl_i2c.h"
#include "types.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

/**
 * Handle for the LPS22DF sensor driver
 *
 * This structure contains the state and configuration for a single instance
 * of the LPS22DF sensor driver. Treat it as an opaque handle and do not
 * access its members directly. Use the provided API functions to interact
 * with the sensor.
 */
typedef struct lps22df_handle lps22df_handle_t;

/**
 * Averaging settings for the LPS22DF sensor.
 */
SL_ENUM(lps22df_avg_t){
    LPS22DF_AVG_4   = 0x00,
    LPS22DF_AVG_8   = 0x01,
    LPS22DF_AVG_16  = 0x02,
    LPS22DF_AVG_32  = 0x03,
    LPS22DF_AVG_64  = 0x04,
    LPS22DF_AVG_128 = 0x05,
    // NO 256!!!!
    LPS22DF_AVG_512 = 0x07,
};

/**
 * Configuration structure for initializing the LPS22DF sensor driver.
 */
typedef struct lps22df_config {
	/// I2C bus handle to use for communication with the sensor.
	sl_i2c_handle_t *i2c_bus;
	/// Set to true if the sensor's I2C address LSB is set (0x5D), false for
	/// default (0x5C).
	bool             addrLSBSet;
	/// Averaging setting for the sensor. See @ref app_lps22df_avg_t for
	/// options.
	lps22df_avg_t    average;
	/// GPIO pin configured for the sensor's data ready (DRDY) interrupt.
	sl_gpio_t       *drdy;
} lps22df_config_t;

/**
 * Initialize the LPS22DF sensor driver with the provided configuration.
 *
 * @param self Pointer to the LPS22DF handle to initialize.
 * @param config Pointer to the configuration structure containing
 *        initialization parameters.
 *
 * @return
 *    - SL_STATUS_OK if the initialization was successful.
 *    - SL_STATUS_INITIALIZATION if there was an error during initialization.
 *
 */
sl_status_t lps22df_init(lps22df_handle_t *self, lps22df_config_t *config);

/**
 * Callback function type for LPS22DF read/write operations.
 *
 * @param status The status of the read/write operation.
 * @param user_data User-defined data passed to the callback function.
 */
typedef void (*lps22df_tx_callback_t)(sl_status_t, void *);

/**
 * Asynchronously read data from the LPS22DF sensor starting from the specified
 * register address.
 *
 * @param self Pointer to the LPS22DF handle.
 * @param start_reg The starting register address to read from.
 * @param count The number of bytes to read.
 * @param buffer Pointer to the buffer where the read data will be stored.
 * @param cb Callback function to be called upon completion of the read
 *        operation.
 * @param user_data User-defined data to be passed to the callback function.
 *
 * @return SL_STATUS_OK if the read operation was successfully initiated, or an
 *         error code if there was an issue starting the operation.
 */
sl_status_t lps22df_read(
    lps22df_handle_t     *self,
    uint8_t               start_reg,
    uint8_t               count,
    uint8_t              *buffer,
    lps22df_tx_callback_t cb,
    void                 *user_data
);

/**
 * @brief Asynchronously write data to the LPS22DF sensor.
 *
 * Asynchronously writes data to the LPS22DF sensor. The first byte of the
 * buffer should be the starting register address, followed by the data to write
 * for each register. The count parameter should include the register address
 * byte and the data bytes to write.
 *
 * @param self Pointer to the LPS22DF handle.
 * @param count The number of bytes to write, including the register address
 *        byte.
 * @param buffer Pointer to the buffer where the first byte is the register
 *        address and the subsequent bytes are the data to write.
 * @param cb Callback function to be called upon completion of the write.
 * @param user_data User-defined data to be passed to the callback function.
 *
 * @return SL_STATUS_OK if the write operation was successfully initiated, or an
 *         error code if there was an issue starting the operation.
 */
sl_status_t lps22df_write(
    lps22df_handle_t     *self,
    uint8_t               count,
    const uint8_t        *buffer,
    lps22df_tx_callback_t cb,
    void                 *user_data
);

/**
 * Synchronously read data from the LPS22DF sensor starting from the specified
 * register address.
 *
 * @param self Pointer to the LPS22DF handle.
 * @param start_reg The starting register address to read from.
 * @param count The number of bytes to read.
 * @param buffer Pointer to the buffer where the read data will be stored.
 *
 * @return SL_STATUS_OK if the read operation was successful, or an error code
 *         if there was an issue during the operation.
 */
sl_status_t lps22df_read_blocking(
    lps22df_handle_t *self, uint8_t start_reg, uint8_t count, uint8_t *buffer
);

/**
 * @brief Synchronously write data to the LPS22DF sensor.
 *
 * Synchronously writes data to the LPS22DF sensor. The first byte of the buffer
 * should be the starting register address, followed by the data to write for
 * each register. The count parameter should include the register address byte
 * and the data bytes to write.
 *
 * @param self Pointer to the LPS22DF handle.
 * @param count The number of bytes to write, including the register address
 *       byte.
 * @param buffer Pointer to the buffer where the first byte is the register
 *        address and the subsequent bytes are the data to write.
 *
 * @return SL_STATUS_OK if the write operation was successful, or an error code
 *        if there was an issue during the operation.
 */
sl_status_t lps22df_write_blocking(
    lps22df_handle_t *self, uint8_t count, const uint8_t *buffer
);

/**
 * Callback function type for LPS22DF pressure readout operations.
 *
 * @param status The status of the readout operation.
 * @param pressure The pressure value read from the sensor, in dPa (0.1
 *        Pa). will be 0xffffffff if there was an error during the readout.
 * @param user_data User-defined data passed to the callback function.
 */
typedef void (*lps22df_readout_callback_t)(
    sl_status_t, pressure_t pressure, void *user_data
);

/**
 * Perform a one-shot pressure measurement with the LPS22DF sensor.
 *
 * @param self Pointer to the LPS22DF handle.
 * @param cb Callback function to be called upon completion of the one-shot
 *       pressure measurement.
 * @param user_data User-defined data to be passed to the callback function.
 *
 * @return SL_STATUS_OK if the one-shot measurement was successfully initiated,
 *         or an error code if there was an issue starting the operation.
 */
sl_status_t lps22df_oneshot(
    lps22df_handle_t *self, lps22df_readout_callback_t cb, void *user_data
);

/**
 * Internal structure for the LPS22DF sensor driver handle. This structure
 * contains the state and configuration for a single instance of the LPS22DF
 * sensor driver. It is used internally by the driver and should not be accessed
 * directly by application code.
 */
struct lps22df_handle {
	sl_i2c_handle_t *i2c_bus;
	uint8_t          address;
	uint8_t          reg_address_buffer;

	volatile lps22df_tx_callback_t tx_callback;
	volatile void                 *tx_user_data;

	sl_gpio_t *dready;
	int32_t    interrupt_number;

	uint8_t read_buffer[3];

	volatile lps22df_readout_callback_t oneshot_callback;
	volatile void                      *oneshot_user_data;
};

#ifdef __cplusplus
}
#endif // __cplusplus
