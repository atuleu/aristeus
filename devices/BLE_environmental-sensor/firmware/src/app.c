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
#include "drivers/spiflash.h"
#include "drivers/stcc4.h"
#include "em_logger.h"
#include "journal.h"
#include "location.h"
#include "pin_config.h"
#include "sl_core.h"
#include "sl_spidrv_instances.h"
#include "types.h"
#include "utils/status.h"
#include <drivers/sht4x.h>
#include <stdio.h>
#include <string.h>

app_handle_t app = {
    .advertising_set_handle = 0xff,
    .is_advertising         = false,
    .data_ready = {.port = LPS22HH_INT_PORT, .pin = LPS22HH_INT_PIN},
    .current_data_point =
        {
            .date        = 0,
            .temperature = GATT_TEMPERATURE_NAN,
            .humidity    = GATT_HUMIDITY_NAN,
            .pressure    = GATT_PRESSURE_NAN,
            .c02         = GATT_CO2_NAN,
        },
    .new_lps22hh_data = false,
    .new_sht4x_data   = false,
    .new_stcc4_data   = false,
    .time_offset      = 0,
};

// The advertising set handle allocated from Bluetooth stack.

void _app_on_lps22hh_readout(
    sl_status_t status, pressure_t pressure, void *user_data
) {
	(void)user_data;
	if (status != SL_STATUS_OK) {
		app_log_warning(
		    "[app] could not read pressure: %s." APP_LOG_NL,
		    sl_status_get_string(status)
		);
		return;
	}
	app_log_info(
	    "[app] got pressure %ld.%03ld." APP_LOG_NL,
	    pressure / 1000,
	    pressure % 1000
	);
	CORE_ATOMIC_SECTION({
		app.current_data_point.pressure = pressure;
		app.new_lps22hh_data            = true;
	});
	app_proceed();
}

void _app_on_sht4x_readout(
    sl_status_t   status,
    temperature_t temperature,
    humidity_t    humidity,
    void         *user_data
) {
	(void)user_data;
	if (status != SL_STATUS_OK) {
		app_log_warning(
		    "[app] sensor readout failure: %s." APP_LOG_NL,
		    sl_status_get_string(status)
		);
		return;
	}
	app_log_info(
	    "[app] got temperature: %d.%02d°C humidity: %d.%01d%%." APP_LOG_NL,
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
}

void _app_on_stcc4_readout(
    sl_status_t status, co2_concentration_t co2, void *user_data
) {
	(void)user_data;
	if (status != SL_STATUS_OK) {
		app_log_warning(
		    "[app] co2 readout failure: %s." APP_LOG_NL,
		    sl_status_get_string(status)
		);
		return;
	}
	app_log_info("[app] got c02 concentration: %dPPM." APP_LOG_NL, co2);
	CORE_ATOMIC_SECTION({
		app.current_data_point.c02 = co2;
		app.new_stcc4_data         = true;
	});
	app_proceed();
}

void _app_on_sensor_timer_timeout(
    sl_sleeptimer_timer_handle_t *timer, void *user_data
) {
	(void)timer;
	(void)user_data;

	em_logger_print();

	app.current_data_point.date = sl_sleeptimer_get_time() + app.time_offset;
	app_log_debug("[app] starting readout." APP_LOG_NL);
	sl_status_t s = sht4x_read_data(
	    &app.sht4x_sensor,
	    SHT4X_MEASURE_HIGH_P,
	    &_app_on_sht4x_readout,
	    NULL
	);
	if (s != SL_STATUS_OK) {
		app_log_error(
		    "[app] could not start SHT4X reading: %s" APP_LOG_NL,
		    sl_status_get_string(s)
		);
	}

	s = lps22hh_oneshot(&app.lps22hh_sensor, &_app_on_lps22hh_readout, NULL);
	if (s != SL_STATUS_OK) {
		app_log_error(
		    "[app] could not start LPS22HH reading: %s" APP_LOG_NL,
		    sl_status_get_string(s)
		);
	}
};

void _app_on_spiflash_deepsleep(sl_status_t status, void *user_data) {
	(void)user_data;
	if (status == SL_STATUS_OK) {
		app_log_info("[app] deep sleep successful." APP_LOG_NL);
	} else {
		app_log_warning(
		    "[app] deep sleep error: %s." APP_LOG_NL,
		    sl_status_get_string(status)
		);
	}
}

// Application Init.
void app_init(void) {
	////////////////////////////////////////////////////////////////////////////
	// Put your additional application init code here!
	//
	// This is called once during start-up.
	////////////////////////////////////////////////////////////////////////////

	sl_status_t status;

	em_logger_init();

	status = spiflash_init(sl_spidrv_spi0_handle);
	app_assert_status(status);

	status = journal_init();
	app_assert_status(status);

	status = i2c_schd_init(&app.i2c0, sl_i2c_i2c0_handle);
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

	stcc4_init_args_t args = {.i2c_bus = &app.i2c0, .address_pin_set = false};
	status                 = stcc4_init(&app.stcc4_sensor, &args);
	if (status != SL_STATUS_OK) {
		app_log_warning("No loop started" APP_LOG_NL);
		return;
	}

	sl_sleeptimer_start_periodic_timer_ms(
	    &app.sensor_timer,
	    SENSOR_READOUT_PERIOD_S * 1000,
	    &_app_on_sensor_timer_timeout,
	    NULL,
	    0,
	    0
	);
	app_log_info("Started read loop" APP_LOG_NL);
}

#define MIN(a, b) ((a) > (b) ? (b) : (a))
#define MAX(a, b) ((a) > (b) ? (a) : (b))

sl_status_t
app_set_legacy_advertiser_data(uint8_t advertising_set, const data_point_t *d) {
	int16_t power;
	sl_bt_system_get_tx_power_setting(NULL, NULL, NULL, &power, NULL);

	uint8_t adv_data[31];
	uint8_t adv_data_len     = 0;
	adv_data[adv_data_len++] = 0x02; // LEN: 2
	adv_data[adv_data_len++] = 0x01; // AD Type: flags
	adv_data[adv_data_len++] = 0x06; // Discoverable Connectable single mode

	adv_data[adv_data_len++] = 0x02;
	adv_data[adv_data_len++] = 0x0A; // AD Type: Xmit Power
	adv_data[adv_data_len++] = MIN((uint16_t)255, power / 10);
	adv_data[adv_data_len++] = sizeof(data_point_t) + 3 + 3;
	adv_data[adv_data_len++] = 0xff; // AD Type: Manufacturer data
	adv_data[adv_data_len++] = 0xff;
	adv_data[adv_data_len++] = 0xff; // non -registered manufacturer

	location_t location = location_get();

	adv_data[adv_data_len++] = location.hive_id;
	adv_data[adv_data_len++] = location.placement;
	adv_data[adv_data_len++] = 100;
	memcpy(&adv_data[adv_data_len], d, sizeof(data_point_t));
	adv_data_len += sizeof(data_point_t);

	app_assert(adv_data_len <= 31, "adv packet too large");

	app_log_debug("[app] new advertised data." APP_LOG_NL);

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

	bool need_update = false;
	CORE_DECLARE_IRQ_STATE;
	CORE_ENTER_ATOMIC();
	if (app.new_lps22hh_data == true && app.new_sht4x_data == true) {
		CORE_EXIT_ATOMIC();
		app_log_debug("[app] starting STCC4 measure." APP_LOG_NL);
		sl_status_t status = stcc4_start_read_sequence(
		    &app.stcc4_sensor,
		    app.current_data_point.temperature,
		    app.current_data_point.humidity,
		    app.current_data_point.pressure,
		    &_app_on_stcc4_readout,
		    NULL
		);
		if (status != SL_STATUS_OK) {
			app_log_error(
			    "Could not start c02 readout: %s." APP_LOG_NL,
			    sl_status_get_string(status)
			);
		}
		CORE_ENTER_ATOMIC();
		app.new_lps22hh_data = false;
		app.new_sht4x_data   = false;
	}
	need_update        = app.new_stcc4_data;
	app.new_stcc4_data = false;
	CORE_EXIT_ATOMIC();

	if (need_update == false) {
		return;
	}

	if (app.current_data_point.date > JOURNAL_MINIMUM_DATE) {
		sl_status_t status =
		    journal_add_record((const data_point_t *)&app.current_data_point);
		if (status != SL_STATUS_OK) {
			app_log_error(
			    "[app] could not save record to journal: %s" APP_LOG_NL,
			    sl_status_get_string(status)
			);
		}
	}

	if (app.is_advertising == true) {
		sl_status_t sc = app_set_legacy_advertiser_data(
		    app.advertising_set_handle,
		    (const data_point_t *)&app.current_data_point
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
		    (const data_point_t *)&app.current_data_point
		);
		app_assert_status(sc);

		// Set advertising interval to 100ms.
		sc = sl_bt_advertiser_set_timing(
		    app.advertising_set_handle,
		    BT_ADV_PERIOD_MS * 1.6, // min. adv. interval (milliseconds / 1.6)
		    BT_ADV_PERIOD_MS * 1.6, // max. adv. interval (milliseconds / 1.6)
		    0,                      // adv. duration
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
		    (const data_point_t *)&app.current_data_point
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
		_app_on_gatt_server_user_read_request(req);
		break;
	}

	case sl_bt_evt_gatt_server_user_write_request_id: {
		sl_bt_evt_gatt_server_user_write_request_t *req =
		    &evt->data.evt_gatt_server_user_write_request;
		_app_on_gatt_server_user_write_request(req);
		break;
	}
	// -------------------------------
	// Default event handler.
	default:
		break;
	}
}

void _app_on_gatt_server_user_read_request(
    sl_bt_evt_gatt_server_user_read_request_t *req
) {
	sl_status_t sc;
	switch (req->characteristic) {
	case gattdb_current_time_epoch: {
		uint32_t time = sl_sleeptimer_get_time() + app.time_offset;
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
	case gattdb_hive_location:
		location_t current_location = location_get();
		sc                          = sl_bt_gatt_server_send_user_read_response(
            req->connection,
            req->characteristic,
            0,
            sizeof(location_t),
            (const uint8_t *)&current_location,
            0
        );
		app_assert_status(sc);
		break;
	default:
		app_log_error("[app] unknown characteristic read request");
	}
}

void _app_on_gatt_server_user_write_request(
    sl_bt_evt_gatt_server_user_write_request_t *req
) {
	uint8_t err = SL_STATUS_OK;
	switch (req->characteristic) {
	case gattdb_current_time_epoch: {
		sl_sleeptimer_timestamp_t now;
		if (req->value.len != sizeof(sl_sleeptimer_timestamp_t)) {
			err = SL_STATUS_BT_ATT_INVALID_ATT_LENGTH & 0xff;
		} else {
			memcpy(&now, req->value.data, sizeof(sl_sleeptimer_timestamp_t));
			app.time_offset = now - sl_sleeptimer_get_time();
		}
		sl_bt_gatt_server_send_user_write_response(
		    req->connection,
		    req->characteristic,
		    err
		);
		break;
	}
	case gattdb_hive_location: {
		location_t new_location;
		if (req->value.len != sizeof(location_t)) {
			err = SL_STATUS_BT_ATT_INVALID_ATT_LENGTH & 0xff;
		} else {
			memcpy(&new_location, req->value.data, sizeof(location_t));
			sl_status_t sc = location_set(new_location);
			if (sc != SL_STATUS_OK) {
				err = SL_STATUS_BT_ATT_WRITE_REQUEST_REJECTED & 0xff;
			}
		}

		sl_bt_gatt_server_send_user_write_response(
		    req->connection,
		    req->characteristic,
		    err
		);
		break;
	}
	default:
		app_log_error("[app] unknown characteristic write request");
	}
}

bool app_is_ok_to_sleep() {
	return i2c_schd_is_ok_to_sleep(&app.i2c0);
}
