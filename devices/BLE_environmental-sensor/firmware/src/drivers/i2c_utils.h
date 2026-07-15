#pragma once

#include "sl_i2c.h"
#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

/**
 * Returns a name for the I2C instance
 *
 * @param instance Pointer to the I2C instance
 *
 * @return Name of the I2C instance, or "I2C<NULL>" if the instance is NULL, or
 *         "I2C<Unknown>" if the instance is not recognized.
 */
const char *i2c_get_instance_name(sl_i2c_handle_t *instance);

/**
 * Takes ownership of the I2C instance.
 *
 * @param instance Pointer to the I2C instance
 *
 * @return
 *   - SL_STATUS_OK if the instance was successfully claimed.
 *   - SL_STATUS_BUSY if the instance is already claimed.
 *   - SL_STATUS_NULL_POINTER if the instance is NULL.
 *   - SL_STATUS_INITIALIZATION if the instance is not initialized.
 *   - SL_STATUS_INVALID_COUNT if the instance is not recognized.
 */
sl_status_t i2c_claim_instance(sl_i2c_handle_t *instance);

/**
 * Releases ownership of the I2C instance.
 *
 * @param instance Pointer to the I2C instance
 *
 * @return
 *   - SL_STATUS_OK if the instance was successfully unclaimed.
 *   - SL_STATUS_NOT_INITIALIZED if the instance was not claimed.
 *   - SL_STATUS_NULL_POINTER if the instance is NULL.
 *   - SL_STATUS_INITIALIZATION if the instance is not initialized.
 *   - SL_STATUS_INVALID_COUNT if the instance is not recognized.
 */

sl_status_t i2c_unclaim_instance(sl_i2c_handle_t *instance);

#ifdef __cplusplus
}
#endif // __cplusplus
