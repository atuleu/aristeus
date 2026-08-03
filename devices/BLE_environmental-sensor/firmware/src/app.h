/*******************************************************************************
 * @file
 * @brief Application interface.
 *******************************************************************************
 * # License
 * <b>Copyright 2025 Silicon Laboratories Inc. www.silabs.com</b>
 *******************************************************************************
 *
 * SPDX-License-Identifier: Zlib
 *
 * The licensor of this software is Silicon Laboratories Inc.
 *
 * This software is provided 'as-is', without any express or implied
 * warranty. In no event will the authors be held liable for any damages
 * arising from the use of this software.
 *
 * Permission is granted to anyone to use this software for any purpose,
 * including commercial applications, and to alter it and redistribute it
 * freely, subject to the following restrictions:
 *
 * 1. The origin of this software must not be misrepresented; you must not
 *    claim that you wrote the original software. If you use this software
 *    in a product, an acknowledgment in the product documentation would be
 *    appreciated but is not required.
 * 2. Altered source versions must be plainly marked as such, and must not be
 *    misrepresented as being the original software.
 * 3. This notice may not be removed or altered from any source distribution.
 *
 ******************************************************************************/

#pragma once

#include "drivers/lps22hh.h"
#include "drivers/sht4x.h"
#include "drivers/stcc4.h"
#include "sl_bt_api.h"
#include "types.h"
#include <stdbool.h>

typedef struct app_handle {
	uint8_t                      advertising_set_handle;
	sl_sleeptimer_timer_handle_t sensor_timer;
	volatile bool                is_advertising;
	i2c_schd_handle_t            i2c0;
	sht4x_handle_t               sht4x_sensor;
	const sl_gpio_t              data_ready;
	lps22hh_handle_t             lps22hh_sensor;
	stcc4_handle_t               stcc4_sensor;

	volatile data_point_t current_data_point;
	volatile bool         new_sht4x_data;
	volatile bool         new_lps22hh_data;
	volatile bool         new_stcc4_data;

} app_handle_t;

extern app_handle_t app;
/******************************************************************************
 * Proceed with execution. (Indicate that it is required to run the application
 * process action.)
 *****************************************************************************/
void                app_proceed(void);

/******************************************************************************
 * Check if it is required to process with execution.
 * @return true if required, false otherwise.
 *****************************************************************************/
bool app_is_process_required(void);

/******************************************************************************
 * Initialize the application.
 *
 * This function initializes the application components.
 *
 * @note Must not be used from ISR context.
 *****************************************************************************/
void app_init_bt(void);

void app_init();

sl_status_t
app_set_legacy_advertiser_data(uint8_t advertising_set, const data_point_t *d);

void _app_on_lps22hh_readout(
    sl_status_t status, pressure_t pressure, void *user_data
);
void _app_on_sht4x_readout(
    sl_status_t   status,
    temperature_t temperature,
    humidity_t    humidity,
    void         *user_data
);

void _app_on_stcc4_readout(
    sl_status_t status, co2_concentration_t co2, void *user_data
);
void _app_on_sensor_timer_timeout(
    sl_sleeptimer_timer_handle_t *timer, void *user_data
);

void _app_on_gatt_server_user_read_request(
    sl_bt_evt_gatt_server_user_read_request_t *req
);
void _app_on_gatt_server_user_write_request(
    sl_bt_evt_gatt_server_user_write_request_t *req
);
