#pragma once

#include "sl_i2c.h"
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

typedef struct app_lps22df_handle app_lps22df_handle_t;

sl_status_t app_lps22df_init(
    app_lps22df_handle_t *self, sl_i2c_handle_t *i2c, bool addrLSBset
);

typedef void (*app_lps22df_tx_callback)(sl_status_t);

sl_status_t app_lps22df_read(
    app_lps22df_handle_t   *self,
    uint8_t                 start_reg,
    uint8_t                 count,
    uint8_t                *buffer,
    app_lps22df_tx_callback cb
);

typedef void (*app_lps22df_read_callback)(sl_status_t);

sl_status_t app_lps22df_write(
    app_lps22df_handle_t   *self,
    uint8_t                 start_reg,
    uint8_t                 count,
    const uint8_t          *buffer,
    app_lps22df_tx_callback cb
);

sl_status_t app_lps22df_read_blocking(
    app_lps22df_handle_t *self,
    uint8_t               start_reg,
    uint8_t               count,
    uint8_t              *buffer
);

sl_status_t app_lps22df_write_blocking(
    app_lps22df_handle_t *self,
    uint8_t               start_reg,
    uint8_t               count,
    const uint8_t        *buffer
);

#ifdef __cplusplus
}
#endif // __cplusplus
