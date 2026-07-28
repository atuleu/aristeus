#include "spiflash.hpp"
#include "sl_status.h"
#include "spidrv.h"

extern "C" {
typedef void (*spiflash_op_callback_t)(sl_status_t, void *);

sl_status_t spiflash_init(SPIDRV_Handle_t spi) {
	(void)spi;
	return SL_STATUS_OK;
}

sl_status_t spiflash_read(
    uint32_t               address,
    uint8_t               *buffer,
    uint32_t               len,
    spiflash_op_callback_t callback,
    void                  *user_data
) {
	return SL_STATUS_BUSY;
}

sl_status_t spiflash_write(
    uint32_t               address,
    const uint8_t         *buffer,
    uint32_t               len,
    spiflash_op_callback_t callback,
    void                  *user_data
) {
	return SL_STATUS_BUSY;
}
}
