#pragma once

#include "drivers/i2c_schd.h"
#include "sl_device_gpio.h"
#include "sl_sleeptimer.h"
#include "sl_status.h"
#include "types.h"
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef struct stcc4_handle stcc4_handle_t;

typedef struct stcc4_init_args {
	i2c_schd_handle_t *i2c_bus;
	bool               address_pin_set;
	const sl_gpio_t    boost_pin;

} stcc4_init_args_t;

sl_status_t stcc4_init(stcc4_handle_t *self, stcc4_init_args_t *args);

typedef void (*stcc4_readout_callback_t)(
    sl_status_t status, co2_concentration_t concentration, void *user_data
);

sl_status_t stcc4_start_read_sequence(
    stcc4_handle_t          *self,
    temperature_t            temperature,
    humidity_t               humidity,
    pressure_t               pressure,
    stcc4_readout_callback_t callback,
    void                    *user_data
);

struct stcc4_handle {
	i2c_schd_handle_t *i2c_bus;
	uint8_t            address;
	sl_gpio_t          boost_pin;

	uint8_t                    buffer[12];
	volatile i2c_tx_callback_t tx_callback;
	volatile void             *tx_user_data;
	uint16_t                   read_delay_ms;
	uint8_t                    read_len;

	volatile stcc4_readout_callback_t read_callback;
	volatile void                    *read_user_data;
	temperature_t                     temperature;
	humidity_t                        humidity;
	pressure_t                        pressure;
	sl_sleeptimer_timer_handle_t      timer;
};

#ifdef __cplusplus
}
#endif
