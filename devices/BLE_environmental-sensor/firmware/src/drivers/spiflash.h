#pragma once

#include "sl_device_gpio.h"
#include "sl_sleeptimer.h"
#include "spidrv.h"
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

#define SPIFLASH_4K  0x1000
#define SPIFLASH_32K 0x8000
#define SPIFLASH_64K 0x10000
#define SPIFLASH_1M  0x100000

sl_status_t spiflash_init(SPIDRV_Handle_t spi);

typedef void (*spiflash_op_callback_t)(sl_status_t, void *user_data);

sl_status_t
spiflash_enter_deepsleep(spiflash_op_callback_t callback, void *user_data);

sl_status_t spiflash_write(
    uint32_t               address,
    const uint8_t         *buffer,
    uint32_t               len,
    spiflash_op_callback_t callback,
    void                  *user_data
);

sl_status_t spiflash_read(
    uint32_t               address,
    uint8_t               *buffer,
    uint32_t               len,
    spiflash_op_callback_t callback,
    void                  *user_data
);

sl_status_t spiflash_erase(
    uint32_t               address,
    uint32_t               length,
    spiflash_op_callback_t callback,
    void                  *user_data
);

#ifdef __cplusplus
}
#endif // __cplusplus
