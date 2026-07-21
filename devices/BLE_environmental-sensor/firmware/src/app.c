/*******************************************************************************
 * @file
 * @brief Core application logic.
 *******************************************************************************
 * # License
 * <b>Copyright 2024 Silicon Laboratories Inc. www.silabs.com</b>
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
#include "app.h"

#include <stdint.h>

#include <sl_bt_api.h>
#include <sl_i2c_instances.h>
#include <sl_main_init.h>
#include <sl_sleeptimer.h>
#include <sl_status.h>

#include <app_assert.h>
#include <app_log.h>

#include <gatt_db.h>

#include "drivers/i2c_schd.h"
#include "drivers/lps22hh.h"
#include "pin_config.h"
#include "sl_core.h"
#include "sl_device_gpio.h"
#include "sl_i2c.h"
#include "types.h"

#include <drivers/sht4x.h>

typedef struct app_handle {
	uint8_t                      advertising_set_handle;
	sl_sleeptimer_timer_handle_t sensor_timer;
	volatile bool                is_advertising;
	i2c_schd_handle_t            i2c0;
	sht4x_handle_t               sht4x_sensor;
	const sl_gpio_t              data_ready;
	lps22hh_handle_t             lps22hh_sensor;

	volatile data_point_t current_data_point;
	volatile bool         new_sht4x_data;
	volatile bool         new_lps22hh_data;

} app_handle_t;

static app_handle_t app = {
    .advertising_set_handle = 0xff,
    .is_advertising         = false,
    .data_ready = {.port = LPS22DF_INT_PORT, .pin = LPS22DF_INT_PIN},
    .current_data_point =
        {.date        = 0,
         .temperature = 0xffff,
         .humidity    = 0xffff,
         .pressure    = 0xffffffff,
         .c02         = 0xffff},
    .new_lps22hh_data = false,
    .new_sht4x_data   = false
};

// The advertising set handle allocated from Bluetooth stack.

sl_status_t app_set_legacy_advertiser_data(
    uint8_t advertising_set, temperature_t temperature, humidity_t humidity
);

void lps22h_read_callback(
    sl_status_t status, pressure_t pressure, void *user_data
) {
	(void)user_data;
	if (status != SL_STATUS_OK) {
		app_log_warning("Could not read pressure: 0x%04lX" APP_LOG_NL, status);
		return;
	}
	app_log_info(
	    "Got pressure %ld.%03ld" APP_LOG_NL,
	    pressure / 1000,
	    pressure % 1000
	);
	CORE_ATOMIC_SECTION({
		app.current_data_point.pressure = pressure;
		app.new_lps22hh_data            = true;
	});
	app_proceed();
}

void sht4x_read_callback(
    sl_status_t   status,
    temperature_t temperature,
    humidity_t    humidity,
    void         *user_data
) {
	(void)user_data;
	if (status != SL_STATUS_OK) {
		app_log_warning("Sensor readout failure: 0x%04lX" APP_LOG_NL, status);
		return;
	}
	app_log_info(
	    "Got temperature: %d.%02d°C humidity: %d.%01d%%" APP_LOG_NL,
	    temperature / 100,
	    temperature % 100,
	    humidity / 10,
	    humidity % 10
	);
	CORE_ATOMIC_SECTION({
		app.current_data_point.temperature = temperature;
		app.current_data_point.humidity    = humidity;
		app.new_sht4x_data                 = true;
	});

	// we need to do something
	app_proceed();

	lps22hh_oneshot(&app.lps22hh_sensor, &lps22h_read_callback, NULL);
}

void start_sensor_readout(
    sl_sleeptimer_timer_handle_t *timer, void *user_data
) {
	(void)timer;
	(void)user_data;

	app.current_data_point.date = sl_sleeptimer_get_time();

	sl_status_t s = sht4x_read_data(
	    &app.sht4x_sensor,
	    SHT4X_MEASURE_HIGH_P,
	    &sht4x_read_callback,
	    NULL
	);
	if (s != SL_STATUS_OK) {
		app_log_error("Could not start sensor reading: 0x%04lX" APP_LOG_NL, s);
	}
};

// Application Init.
void app_init(void) {
	/////////////////////////////////////////////////////////////////////////////
	// Put your additional application init code here! // This is called once
	// during start-up.                                    //
	/////////////////////////////////////////////////////////////////////////////

	sl_status_t status = i2c_schd_init(&app.i2c0, sl_i2c_i2c0_handle);
	app_assert_status(status);

	status = sht4x_init(&app.sht4x_sensor, &app.i2c0, SHT4X_BASE_ADDR);
	if (status != SL_STATUS_OK) {
		app_log_warning("No loop started" APP_LOG_NL);
		return;
	}
	lps22hh_config_t config = {
	    .i2c_bus       = &app.i2c0,
	    .addrLSBSet    = false,
	    .interrupt_pin = &app.data_ready,
	};
	status = lps22hh_init(&app.lps22hh_sensor, &config);
	if (status != SL_STATUS_OK) {
		app_log_warning("No loop started" APP_LOG_NL);
		return;
	}

	sl_sleeptimer_start_periodic_timer_ms(
	    &app.sensor_timer,
	    1000,
	    &start_sensor_readout,
	    NULL,
	    0,
	    0
	);
	app_log_info("Started read loop" APP_LOG_NL);
}

sl_status_t app_set_legacy_advertiser_data(
    uint8_t advertising_set, temperature_t temperature, humidity_t humidity
) {
	int16_t power;
	sl_bt_system_get_tx_power_setting(NULL, NULL, NULL, &power, NULL);

	uint8_t adv_data[31];
	uint8_t adv_data_len     = 0;
	adv_data[adv_data_len++] = 0x02; // LEN: 2
	adv_data[adv_data_len++] = 0x01; // AD Type: flags
	adv_data[adv_data_len++] = 0x06; // Discoverable Connectable single mode

	adv_data[adv_data_len++] = 0x0a; // LEN: 10
	// AD Type: Service data
	adv_data[adv_data_len++] = 0x16;
	// BT Homew service
	adv_data[adv_data_len++] = 0xD2;
	adv_data[adv_data_len++] = 0xFC;
	// non encrypted data
	adv_data[adv_data_len++] = 0x40;
	// BTHome temperature
	adv_data[adv_data_len++] = 0x02;
	adv_data[adv_data_len++] = (temperature >> 8) & 0xff;
	adv_data[adv_data_len++] = temperature & 0xff;

	// BTHome humidity
	adv_data[adv_data_len++] = 0x03;
	adv_data[adv_data_len++] = (humidity >> 8) & 0xff;
	adv_data[adv_data_len++] = humidity & 0xff;

	adv_data[adv_data_len++] = 0x02; // LEN: 2
	// AD Type: Tx Power
	adv_data[adv_data_len++] = 0x0A;
	// Power in dBm
	adv_data[adv_data_len++] = power / 10;

	app_assert(adv_data_len < (31 - 5), "adv packet too large");

	size_t name_len;

	sl_status_t sc = sl_bt_gatt_server_read_attribute_value(
	    gattdb_device_name,
	    0,
	    sizeof(adv_data) - adv_data_len - 2,
	    &name_len,
	    &adv_data[adv_data_len]
	);

	app_assert_status_f(sc);

	if (name_len < (29U - adv_data_len)) {
		adv_data[adv_data_len++] = name_len + 2;
		adv_data[adv_data_len++] = 0x09;
		adv_data_len += name_len;
	} else {
		adv_data[adv_data_len] = 31 - adv_data_len;
		adv_data_len++;
		adv_data[adv_data_len++] = 0x08;
		adv_data_len             = 31;
	}

	app_log_debug("new advertised data" APP_LOG_NL);

	return sl_bt_legacy_advertiser_set_data(
	    advertising_set,
	    sl_bt_advertiser_advertising_data_packet,
	    adv_data_len,
	    adv_data
	);
}

// Application Process Action.
void app_process_action(void) {
	i2c_schd_process_action(&app.i2c0);

	if (app_is_process_required() == false) {
		return;
	}

	lps22hh_process_action(&app.lps22hh_sensor);

	bool need_update;
	CORE_ATOMIC_SECTION({
		need_update          = app.new_lps22hh_data || app.new_sht4x_data;
		app.new_lps22hh_data = false;
		app.new_sht4x_data   = false;
	});

	if (need_update == false) {
		return;
	}

	/////////////////////////////////////////////////////////////////////////////
	// Put your additional application code here! This is will run each time
	// app_proceed() is called.
	//
	// Do not call blocking functions from here!
	/////////////////////////////////////////////////////////////////////////////
	if (app.is_advertising == true) {
		sl_status_t sc = app_set_legacy_advertiser_data(
		    app.advertising_set_handle,
		    app.current_data_point.temperature,
		    app.current_data_point.humidity
		);
		app_assert_status_f(sc);
	}
}

/****************************************************************************
 * Bluetooth stack event handler.
 * This overrides the default weak implementation.
 *
 * @param[in] evt Event coming from the Bluetooth stack.
 *****************************************************************************/
void sl_bt_on_event(sl_bt_msg_t *evt) {
	sl_status_t sc;

	switch (SL_BT_MSG_ID(evt->header)) {
	// -------------------------------
	// This event indicates the device has started and the radio is ready.
	// Do not call any stack command before receiving this boot event!
	case sl_bt_evt_system_boot_id:
		// Create an advertising set.
		sc = sl_bt_advertiser_create_set(&app.advertising_set_handle);

		app_assert_status(sc);

		// Generate data for advertising
		sc = app_set_legacy_advertiser_data(
		    app.advertising_set_handle,
		    0xffff,
		    0xffff
		);
		app_assert_status(sc);

		// Set advertising interval to 100ms.
		sc = sl_bt_advertiser_set_timing(
		    app.advertising_set_handle,
		    160, // min. adv. interval (milliseconds / 1.6)
		    160, // max. adv. interval (milliseconds / 1.6)
		    0,   // adv. duration
		    0
		); // max. num. adv. events
		app_assert_status(sc);
		// Start advertising and enable connections.
		sc = sl_bt_legacy_advertiser_start(
		    app.advertising_set_handle,
		    sl_bt_legacy_advertiser_connectable
		);
		app.is_advertising = true;
		app_assert_status(sc);
		break;

	// -------------------------------
	// This event indicates that a new connection was opened.
	case sl_bt_evt_connection_opened_id:
		app_log_info("connected" APP_LOG_NL);
		app.is_advertising = false;
		break;

	// -------------------------------
	// This event indicates that a connection was closed.
	case sl_bt_evt_connection_closed_id:
		app_log_info("disconnected" APP_LOG_NL);
		// Generate data for advertising
		sc = app_set_legacy_advertiser_data(
		    app.advertising_set_handle,
		    0xffff,
		    0xffff
		);

		app_assert_status(sc);

		// Restart advertising after client has disconnected.
		sc = sl_bt_legacy_advertiser_start(
		    app.advertising_set_handle,
		    sl_bt_legacy_advertiser_connectable
		);
		app.is_advertising = true;
		app_assert_status(sc);
		break;

	///////////////////////////////////////////////////////////////////////////
	// Add additional event handlers here as your application requires!      //
	///////////////////////////////////////////////////////////////////////////
	case sl_bt_evt_gatt_server_user_read_request_id: {
		sl_bt_evt_gatt_server_user_read_request_t *req =
		    &evt->data.evt_gatt_server_user_read_request;
		switch (req->characteristic) {
		case gattdb_current_time_epoch: {
			uint32_t time = sl_sleeptimer_get_time();
			sc            = sl_bt_gatt_server_send_user_read_response(
                req->connection,
                req->characteristic,
                0,
                sizeof(time),
                (const uint8_t *)&time,
                0
            );
			app_assert_status(sc);
			break;
		}
		default:
			app_log_error("unknown characteristic read request");
		}
		break;
	}
	case sl_bt_evt_gatt_server_user_write_request_id: {
		sl_bt_evt_gatt_server_user_write_request_t *req =
		    &evt->data.evt_gatt_server_user_write_request;
		switch (req->characteristic) {
		case gattdb_current_time_epoch: {
			sl_bt_gatt_server_send_user_write_response(
			    req->connection,
			    req->characteristic,
			    0 // TODO: use success enum.
			);
			break;
		}
		default:
			app_log_error("unknown characteristic write request");
		}
		break;
	}

	// -------------------------------
	// Default event handler.
	default:
		break;
	}
}
