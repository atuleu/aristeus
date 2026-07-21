#pragma once

#include "sl_i2c.h"
#include "sl_sleeptimer.h"
#include <stdint.h>
#if defined(__cplusplus)
extern "C" {
#endif // defined(__cplusplus)

#ifndef I2C_SCHEDULER_QUEUE_SIZE
#define I2C_SCHEDULER_QUEUE_SIZE 8
#endif

/**
 * Opaque handle for the I2C scheduler. Use `i2c_schd_init()` to initialize it.
 */
typedef struct i2c_schd_handle i2c_schd_handle_t;

/**
 * Initialize an I2C scheduler handle with the given I2C bus.
 *
 * @param[in] handle Pointer to the I2C scheduler handle to initialize.
 * @param[in] i2c_bus Pointer to the I2C bus handle to use for communication.
 *
 * @return SL_STATUS_OK on success, or an error code on failure.
 */
sl_status_t i2c_schd_init(i2c_schd_handle_t *handle, sl_i2c_handle_t *i2c_bus);

/**
 * Returns a name for the I2C instance
 *
 * @param instance Pointer to the I2C instance
 *
 * @return Name of the I2C instance, or "I2C<NULL>" if the instance is NULL, or
 *         "I2C<Unknown>" if the instance is not recognized.
 */
const char *i2c_schd_get_instance_name(i2c_schd_handle_t *self);

/**
 * Process any pending I2C actions in the scheduler. This function should be
 * called in app_process(). It should not be protected by the proceed guard.
 *
 * @param[in] self Pointer to the I2C scheduler handle.
 */
void i2c_schd_process_action(i2c_schd_handle_t *self);

/**
 * @brief Status codes for I2C transmission operations.
 */
SL_ENUM(i2c_tx_status_t){
    I2C_TX_OK = 0,             /// Transmission succesful
    I2C_TX_TIMER_ERROR,        /// Could not start timeout timer
    I2C_TX_START_ERROR,        /// Could not start the transaction
    I2C_TX_PARAMETER_ERROR,    /// Transaction was not correctly parameterized
    I2C_TX_FOLLOWER_ACK_ERROR, /// Did not receive an ACK
    I2C_TX_BUS_ERROR,          /// COuld not transmit the transaction
    I2C_TX_TIMEOUT,            /// Transaction timeouted
};

/**
 * @brief Callback type for I2C transmission operations.
 *
 * @param status     Status of the I2C transmission operation.
 * @param buffer Pointer to the data buffer involved in the operation. Only
 *        valid for succesful read operation, NULL otherwise.
 * @param len Length of the data buffer involved in the operation. Only valid
 *        for succesful read operation, 0 otherwise.
 * @param user_data user defined context data for the callback.
 */
typedef void (*i2c_tx_callback_t)(
    i2c_tx_status_t status, const uint8_t *buffer, uint8_t len, void *user_data
);

/**
 * Schedule an I2C receive operation.
 *
 * @param[in] self Pointer to the I2C scheduler handle.
 * @param[in] address I2C address of the device to receive data from.
 * @param[in] buffer Pointer to the buffer to store received data.
 * @param[in] len Length of the data to receive.
 * @param[in] callback Callback function to be called upon completion of the
 *            operation.
 * @param[in] user_data User-defined data to be passed to the callback function.
 *
 * @return SL_STATUS_OK on successful scheduling, or an error code on failure.
 */

sl_status_t i2c_schd_receive(
    i2c_schd_handle_t *self,
    uint8_t            address,
    uint8_t           *buffer,
    uint8_t            len,
    i2c_tx_callback_t  callback,
    void              *user_data
);

/**
 * Schedule an I2C send operation.
 *
 * @param[in] self Pointer to the I2C scheduler handle.
 * @param[in] address I2C address of the device to receive data from.
 * @param[in] buffer Pointer to the buffer to read data from.
 * @param[in] len Length of the data to send.
 * @param[in] callback Callback function to be called upon completion of the
 *            operation.
 * @param[in] user_data User-defined data to be passed to the callback function.
 *
 * @return SL_STATUS_OK on successful scheduling, or an error code on failure.
 */
sl_status_t i2c_schd_send(
    i2c_schd_handle_t *self,
    uint8_t            address,
    const uint8_t     *buffer,
    uint8_t            len,
    i2c_tx_callback_t  callback,
    void              *user_data
);

/**
 * Schedule an I2C transfer operation (write followed by read using repeated
 * start).
 *
 * @param[in] self Pointer to the I2C scheduler handle.
 * @param[in] address I2C address of the device to communicate with.
 * @param[in] write_buffer Pointer to the buffer to read data from.
 * @param[in] write_len Length of the data to send.
 * @param[in] read_buffer Pointer to the buffer to store received data.
 * @param[in] read_len Length of the data to receive.
 * @param[in] callback Callback function to be called upon completion of the
 *            operation.
 * @param[in] user_data User-defined data to be passed to the callback function.
 *
 * @return SL_STATUS_OK on successful scheduling, or an error code on failure.
 */
sl_status_t i2c_schd_transfer(
    i2c_schd_handle_t *self,
    uint8_t            address,
    const uint8_t     *write_buffer,
    uint8_t            write_len,
    uint8_t           *read_buffer,
    uint8_t            read_len,
    i2c_tx_callback_t  callback,
    void              *user_data
);

/**
 * Synchronous blocking receive operation. This function will block until the
 * operation is completed or an error occurs.
 *
 * @note This is a mere wrapper around sl_i2c_leader_receive_blocking() but
 * performs a check to ensure that the scheduler is not busy with another
 * operation. Please not use directly the latter function, as it will not check
 * for the scheduler state.
 *
 * @param[in] self Pointer to the I2C scheduler handle.
 * @param[in] address I2C address of the device to receive data from.
 * @param[in] buffer Pointer to the buffer to store received data.
 * @param[in] len Length of the data to receive.
 *
 * @return SL_STATUS_OK on successful operation, or an error code on failure.
 */

sl_status_t i2c_schd_receive_blocking(
    i2c_schd_handle_t *self, uint8_t address, uint8_t *buffer, uint8_t len
);

/**
 * Synchronous blocking send operation. This function will block until the
 * operation is completed or an error occurs.
 *
 * @note This is a mere wrapper around sl_i2c_leader_send_blocking() but
 * performs a check to ensure that the scheduler is not busy with another
 * operation. Please not use directly the latter function, as it will not check
 * for the scheduler state.
 *
 * @param[in] self Pointer to the I2C scheduler handle.
 * @param[in] address I2C address of the device to receive data from.
 * @param[in] buffer Pointer to the buffer to read data from.
 * @param[in] len Length of the data to send.
 *
 * @return SL_STATUS_OK on successful operation, or an error code on failure.
 */

sl_status_t i2c_schd_send_blocking(
    i2c_schd_handle_t *self, uint8_t address, const uint8_t *buffer, uint8_t len
);

/**
 * Synchronous blocking transfer operation (write followed by read using
 * repeated start). This function will block until the operation is completed or
 * an error occurs.
 *
 * @note This is a mere wrapper around sl_i2c_leader_transfer_blocking() but
 * performs a check to ensure that the scheduler is not busy with another
 * operation. Please not use directly the latter function, as it will not check
 * for the scheduler state.
 *
 * @param[in] self Pointer to the I2C scheduler handle.
 * @param[in] address I2C address of the device to receive data from.
 * @param[in] write_buffer Pointer to the buffer to read data from.
 * @param[in] write_len Length of the data to send.
 * @param[in] read_buffer Pointer to the buffer to store received data.
 * @param[in] read_len Length of the data to receive.
 *
 * @return SL_STATUS_OK on successful operation, or an error code on failure.
 */

sl_status_t i2c_schd_transfer_blocking(
    i2c_schd_handle_t *self,
    uint8_t            address,
    const uint8_t     *write_buffer,
    uint8_t            write_len,
    uint8_t           *read_buffer,
    uint8_t            read_len
);

/**
 * Opaque handle for an I2C transmission operation. This structure is used
 * internally. The implementation is public only for providing static allocation
 * of the queue. Do not use this structure directly.
 */
typedef struct i2c_tx_handle {
	uint8_t           address;
	const uint8_t    *write_buffer;
	uint8_t           write_len;
	uint8_t          *read_buffer;
	uint8_t           read_len;
	i2c_tx_callback_t callback;
	void             *user_data;
} i2c_tx_handle_t;

/**
 * Opaque handle for the I2C scheduler. This structure is used internally. This
 * structure is public only for providing static allocation of the queue. Do not
 * use directly.
 */
struct i2c_schd_handle {
	sl_i2c_handle_t             *i2c_bus;
	sl_i2c_init_params_t         init_params;
	volatile i2c_tx_handle_t    *current_tx;
	i2c_tx_handle_t              queue[I2C_SCHEDULER_QUEUE_SIZE];
	uint8_t                      head, tail;
	sl_sleeptimer_timer_handle_t timer;
	volatile bool                stuck_flag;
	const char                  *name;
};

#if defined(__cplusplus)
}
#endif // defined(__cplusplus)
