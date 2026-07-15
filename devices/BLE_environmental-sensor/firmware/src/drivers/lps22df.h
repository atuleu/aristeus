#pragma once

#include "sl_device_gpio.h"
#include "sl_i2c.h"
#include "types.h"
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

typedef struct app_lps22df_handle app_lps22df_handle_t;

SL_ENUM(app_lps22df_avg_t){
    LPS22DF_AVG_4   = 0x00,
    LPS22DF_AVG_8   = 0x01,
    LPS22DF_AVG_16  = 0x02,
    LPS22DF_AVG_32  = 0x03,
    LPS22DF_AVG_64  = 0x04,
    LPS22DF_AVG_128 = 0x05,
    // NO 256!!!!
    LPS22DF_AVG_512 = 0x07,
};

typedef struct app_lps22df_config {
	sl_i2c_handle_t  *i2c_bus;
	bool              addrLSBSet;
	app_lps22df_avg_t average;
	sl_gpio_t        *drdy;
} app_lps22df_config_t;

sl_status_t
app_lps22df_init(app_lps22df_handle_t *self, app_lps22df_config_t *config);

typedef void (*app_lps22df_tx_callback_t)(sl_status_t, void *);

sl_status_t app_lps22df_read(
    app_lps22df_handle_t     *self,
    uint8_t                   start_reg,
    uint8_t                   count,
    uint8_t                  *buffer,
    app_lps22df_tx_callback_t cb,
    void                     *user_data
);

sl_status_t app_lps22df_write(
    app_lps22df_handle_t     *self,
    uint8_t                   count,
    const uint8_t            *buffer,
    app_lps22df_tx_callback_t cb,
    void                     *user_data
);

sl_status_t app_lps22df_read_blocking(
    app_lps22df_handle_t *self,
    uint8_t               start_reg,
    uint8_t               count,
    uint8_t              *buffer
);

sl_status_t app_lps22df_write_blocking(
    app_lps22df_handle_t *self, uint8_t count, const uint8_t *buffer
);

typedef void (*app_lps22df_readout_callback_t)(
    sl_status_t, pressure_t pressure, void *user_data
);

sl_status_t app_lps22df_oneshot(
    app_lps22df_handle_t          *self,
    app_lps22df_readout_callback_t cb,
    void                          *user_data
);

struct app_lps22df_handle {
	sl_i2c_handle_t *i2c_bus;
	uint8_t          address;
	uint8_t          reg_address_buffer;

	volatile app_lps22df_tx_callback_t tx_callback;
	volatile void                     *tx_user_data;

	sl_gpio_t *dready;
	int32_t    interrupt_number;

	uint8_t read_buffer[3];

	volatile app_lps22df_readout_callback_t oneshot_callback;
	volatile void                          *oneshot_user_data;
};

#ifdef __cplusplus
}
#endif // __cplusplus
