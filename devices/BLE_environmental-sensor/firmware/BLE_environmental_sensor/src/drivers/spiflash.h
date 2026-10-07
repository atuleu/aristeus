#pragma once

#include "spidrv.h"
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

#define SPIFLASH_4K  0x1000
#define SPIFLASH_32K 0x8000
#define SPIFLASH_64K 0x10000
#define SPIFLASH_1M  0x100000

#define SPIFLASH_SIZE SPIFLASH_1M

#define SPIFLASH_MAX_ADDRESS 0x0FFFFF

/**
 * Initialize the spiflash driver
 */
sl_status_t spiflash_init(SPIDRV_Handle_t spi);

/**
 * Callback for any operation on the flash
 *
 * @param status The status of the operation
 * @param user_data The user data passed to the operation
 */
typedef void (*spiflash_op_callback_t)(sl_status_t, void *user_data);

/**
 * Asynchronously make the chip enter deepsleep. Will automatically wake-up
 * asynchronously on any further operation.
 *
 * @param callback The callback to call when the operation is complete.
 * @param user_data The user data to pass to the callback
 *
 * @return SL_STATUS_BUSY if the chip is already in deepsleep, any error on the
 *       SPI bus will be returned as well.
 */
sl_status_t
spiflash_enter_deepsleep(spiflash_op_callback_t callback, void *user_data);

/**
 * Asynchronously write data on the chip. The range should not cross a 256 bytes
 * boundary.
 *
 * @param address The address to write to
 * @param buffer The buffer to write
 * @param length The length of the buffer
 * @param callback The callback to call when the operation is complete.
 * @param user_data The user data to pass to the callback
 *
 * @return SL_STATUS_INVALID_RANGE if the address is out of range, or if the
 *       length is too long, or if the range crosses a 256 bytes boundary, any
 *       error on the SPI bus will be returned as well.
 */

sl_status_t spiflash_write(
    uint32_t               address,
    const uint8_t         *buffer,
    uint32_t               length,
    spiflash_op_callback_t callback,
    void                  *user_data
);

/**
 * Asynchronously read data from the chip.
 *
 * @param address The address to write to
 * @param buffer The buffer to write
 * @param length The length of the buffer
 * @param callback The callback to call when the operation is complete.
 * @param user_data The user data to pass to the callback
 *
 * @return SL_STATUS_INVALID_RANGE if the address is out of range, or if the
 *       length is too long, any error on the SPI bus will be returned as well.
 */
sl_status_t spiflash_read(
    uint32_t               address,
    uint8_t               *buffer,
    uint32_t               length,
    spiflash_op_callback_t callback,
    void                  *user_data
);
/**
 * Asynchronously erase data from the chip. Only full sector, blocks or chip
 * erase are supported. You have to set length to one of this size, and make
 * sure the address is at the right boundary otherwise an error would be
 * returned.
 *
 * @param address The address to erase, must align to a valid boundary for the
 *        size.
 * @param length The length to erase, must be one of the valid size
 *        (SPIFLASH_4K,SPIFLASH_16K,SPIFLASH_32K,SPIFLASH_1M)
 * @param callback The callback to call when the operation is complete.
 * @param user_data The user data to pass to the callback
 *
 * @return SL_STATUS_INVALID_COUNT if the length is not a valid size, or
 *         SL_STATUS_INVALID_RANGE if the address is not aligned to the size,
 *         any error on the SPI bus will be returned as well.
 */
sl_status_t spiflash_erase(
    uint32_t               address,
    uint32_t               length,
    spiflash_op_callback_t callback,
    void                  *user_data
);

#ifdef __cplusplus
}
#endif // __cplusplus
