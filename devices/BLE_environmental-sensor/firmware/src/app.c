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

#include "app_es.h"
#include "batt_monitor.h"
#include "drivers/i2c_schd.h"
#include "drivers/spiflash.h"
#include "em_logger.h"
#include "journal.h"
#include "location.h"
#include "pin_config.h"
#include "sl_spidrv_instances.h"
#include "types.h"
#include "utils/status.h"
#include <drivers/sht4x.h>
#include <stdio.h>
#include <string.h>

app_handle_t app = {
    .advertising_set_handle = SL_BT_INVALID_ADVERTISING_SET_HANDLE,
    .connection             = SL_BT_INVALID_CONNECTION_HANDLE,
};

// The advertising set handle allocated from Bluetooth stack.

// Application Init.
void app_init(void) {

	sl_status_t status;

	em_logger_init();

	app_log_info(
	    "[app] sleeptimer frequency is %ld 1 ticks = %ld us." APP_LOG_NL,
	    sl_sleeptimer_get_timer_frequency(),
	    1000000 / sl_sleeptimer_get_timer_frequency()
	);

	status = spiflash_init(sl_spidrv_spi0_handle);
	app_assert_status(status);

	status = journal_init();
	app_assert_status(status);

	status = i2c_schd_init(&app.i2c0, sl_i2c_i2c0_handle);
	app_assert_status(status);

	app_es_config_t es_config = {
	    .i2c_bus = &app.i2c0,
	    .data_ready_pin =
	        {
	            .pin  = LPS22HH_INT_PIN,
	            .port = LPS22HH_INT_PORT,
	        },
	    .readout_period_ms = SENSOR_READOUT_PERIOD_S * 1000,
	    .callback          = &_app_on_es_readout,
	};

	status = app_es_init(&es_config);
	app_assert_status(status);
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
	adv_data[adv_data_len++] = batt_monitor_get_current_level();
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
	journal_process_action();
	app_es_process_action();

	if (app_is_process_required() == false) {
		return;
	}
}

/****************************************************************************
 * Bluetooth stack event handler.
 * This overrides the default weak implementation.
 *
 * @param[in] evt Event coming from the Bluetooth stack.
 *****************************************************************************/
void sl_bt_on_event(sl_bt_msg_t *evt) {
	switch (SL_BT_MSG_ID(evt->header)) {
	// -------------------------------
	// This event indicates the device has started and the radio is ready.
	// Do not call any stack command before receiving this boot event!
	case sl_bt_evt_system_boot_id:
		_app_on_bt_system_boot();
		break;

	case sl_bt_evt_connection_opened_id:
		_app_on_bt_connection_opened(&evt->data.evt_connection_opened);
		break;

	case sl_bt_evt_connection_closed_id:
		_app_on_bt_connection_closed(&evt->data.evt_connection_closed);
		break;
	case sl_bt_evt_gatt_server_characteristic_status_id:
		_app_connection_wd_reset();
		_app_on_gatt_server_characteristic_status(
		    &evt->data.evt_gatt_server_characteristic_status
		);
		break;
	case sl_bt_evt_gatt_server_attribute_value_id:
		_app_connection_wd_reset();
		// do nothing.
		break;
	case sl_bt_evt_gatt_server_user_read_request_id: {
		_app_connection_wd_reset();
		sl_bt_evt_gatt_server_user_read_request_t *req =
		    &evt->data.evt_gatt_server_user_read_request;
		_app_on_gatt_server_user_read_request(req);
		break;
	}

	case sl_bt_evt_gatt_server_user_write_request_id: {
		_app_connection_wd_reset();
		sl_bt_evt_gatt_server_user_write_request_t *req =
		    &evt->data.evt_gatt_server_user_write_request;
		_app_on_gatt_server_user_write_request(req);
		break;
	}

	case sl_bt_evt_system_external_signal_id:
		uint32_t signals = evt->data.evt_system_external_signal.extsignals;
		_app_on_external_signals(signals);
		break;

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
		sl_sleeptimer_timestamp_t time = app_es_get_current_unix_time();
		sc = sl_bt_gatt_server_send_user_read_response(
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

bool app_is_ok_to_sleep(void) {
	return journal_is_ok_to_sleep() && i2c_schd_is_ok_to_sleep(&app.i2c0) &&
	       app_es_is_ok_to_sleep();
}

void _app_on_es_readout(sl_status_t status, const data_point_t *point) {
	if (status != SL_STATUS_OK) {
		return;
	}
	if (point->date > JOURNAL_MINIMUM_DATE) {
		sl_status_t status = journal_add_record(point);
		if (status != SL_STATUS_OK) {
			app_log_error(
			    "[app] could not save new record to journal: %s" APP_LOG_NL,
			    sl_status_get_string(status)
			);
		}
	}

	if (_app_is_connected() == false) {
		sl_status_t status =
		    app_set_legacy_advertiser_data(app.advertising_set_handle, point);
		if (status != SL_STATUS_OK) {
			app_log_error(
			    "[app] could not setup advertisement data: %s." APP_LOG_NL,
			    sl_status_get_string(status)
			);
		}
	}
}

bool _app_is_connected() {
	return app.connection != SL_BT_INVALID_CONNECTION_HANDLE;
}

void _app_connection_wd_reset() {
	CORE_ATOMIC_SECTION({
		sl_sleeptimer_restart_timer_ms(
		    &app.connection_wd,
		    APP_CONNECTION_TIMEOUT_MS,
		    &_app_on_connection_wd_timeout,
		    NULL,
		    0,
		    0
		);
		app.wd_fired = false;
	});
	app_log_debug("[app] connection WD reset." APP_LOG_NL);
}

void _app_connection_wd_start() {
	CORE_ATOMIC_SECTION({
		sl_sleeptimer_start_timer_ms(
		    &app.connection_wd,
		    APP_CONNECTION_TIMEOUT_MS,
		    &_app_on_connection_wd_timeout,
		    NULL,
		    0,
		    0
		);
		app.wd_fired = false;
	});
	app_log_debug("[app] connection WD started." APP_LOG_NL);
}

void _app_connection_wd_stop() {
	CORE_ATOMIC_SECTION({
		sl_sleeptimer_start_timer_ms(
		    &app.connection_wd,
		    APP_CONNECTION_TIMEOUT_MS,
		    &_app_on_connection_wd_timeout,
		    NULL,
		    0,
		    0
		);
		app.wd_fired = false;
	});
	app_log_debug("[app] connection WD stopped." APP_LOG_NL);
}

void _app_on_connection_wd_timeout(
    sl_sleeptimer_timer_handle_t *timer, void *user_data
) {
	(void)timer;
	(void)user_data;
	CORE_ATOMIC_SECTION({
		sl_bt_external_signal(APP_CONNECTION_WD_SIGNAL);
		app.wd_fired = true;
	});
}

void _app_on_external_signals(uint32_t events) {
	if ((events & APP_CONNECTION_WD_SIGNAL) != 0x00) {
		if (app.wd_fired && _app_is_connected()) {
			sl_bt_connection_close(app.connection);
			app_log_warning(
			    "[app] closing connection after %d.%03ds of "
			    "inactivity." APP_LOG_NL,
			    APP_CONNECTION_TIMEOUT_MS / 1000,
			    APP_CONNECTION_TIMEOUT_MS % 1000

			);
		}
	}
}

void _app_on_bt_system_boot() {
	// Create an advertising set.
	sl_status_t status =
	    sl_bt_advertiser_create_set(&app.advertising_set_handle);

	app_assert_status(status);

	// Generate data for advertising
	status = app_set_legacy_advertiser_data(
	    app.advertising_set_handle,
	    app_es_current_data()

	);
	app_assert_status(status);

	// Set advertising interval to 100ms.
	status = sl_bt_advertiser_set_timing(
	    app.advertising_set_handle,
	    BT_ADV_PERIOD_MS * 1.6, // min. adv. interval (milliseconds / 1.6)
	    BT_ADV_PERIOD_MS * 1.6, // max. adv. interval (milliseconds / 1.6)
	    0,                      // adv. duration
	    0
	); // max. num. adv. events
	app_assert_status(status);
	// Start advertising and enable connections.
	status = sl_bt_legacy_advertiser_start(
	    app.advertising_set_handle,
	    sl_bt_legacy_advertiser_connectable
	);
	app.connection = SL_BT_INVALID_CONNECTION_HANDLE;
	app_assert_status(status);
}

void _app_on_bt_connection_opened(sl_bt_evt_connection_opened_t *evt) {
	app.connection = evt->connection;
	app_log_info(
	    "[app] connected with %02X:%02X:%02X:%02X:%02X:%02X." APP_LOG_NL,
	    evt->address.addr[0],
	    evt->address.addr[1],
	    evt->address.addr[2],
	    evt->address.addr[3],
	    evt->address.addr[4],
	    evt->address.addr[5]
	);
	_app_connection_wd_start();
}

void _app_on_bt_connection_closed(sl_bt_evt_connection_closed_t *evt) {
	(void)evt;
	_app_connection_wd_stop();
	app_log_info("[app] disconnected." APP_LOG_NL);
	app.connection = SL_BT_INVALID_CONNECTION_HANDLE;
	// Generate data for advertising

	sl_status_t status = app_set_legacy_advertiser_data(
	    app.advertising_set_handle,
	    app_es_current_data()
	);

	app_assert_status(status);

	// Restart advertising after client has disconnected.
	status = sl_bt_legacy_advertiser_start(
	    app.advertising_set_handle,
	    sl_bt_legacy_advertiser_connectable
	);

	app_assert_status(status);
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
			app_es_set_current_unix_time(now);
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
	case gattdb_record_access_control_point:
		_app_racp_user_write_request_handler(req);
		break;
	default:
		app_log_error("[app] unknown characteristic write request");
	}
}

void _app_on_gatt_server_characteristic_status(
    sl_bt_evt_gatt_server_characteristic_status_t *evt
) {
	switch (evt->characteristic) {
	case gattdb_stream_data:
		_app_stream_data_notification_handler(evt);
		break;
	case gattdb_record_access_control_point:
		_app_racp_notification_handler(evt);
		break;
	default:
		app_log_warning(
		    "[app] unknwon GATT characteristic status %d." APP_LOG_NL,
		    evt->characteristic
		);
	}
}

void _app_stream_data_notification_handler(
    sl_bt_evt_gatt_server_characteristic_status_t *evt
) {
	app_log_debug(
	    "[app] stream data notification handler %d %x." APP_LOG_NL,
	    evt->status_flags,
	    evt->client_config_flags
	);
}

void _app_racp_notification_handler(
    sl_bt_evt_gatt_server_characteristic_status_t *evt
) {
	app_log_debug(
	    "[app] RACP notification handler %d %x." APP_LOG_NL,
	    evt->status_flags,
	    evt->client_config_flags
	);
}

SL_ENUM(racp_opcode_t){
    racp_opcode_not_supported            = 0,
    racp_opcode_report_records           = 1,
    racp_opcode_delete_records           = 2,
    racp_opcode_abort_operation          = 3,
    racp_opcode_report_number_of_records = 4,
};

SL_ENUM(racp_rsp_t){
    racp_rsp_code_succeed         = 0x01,
    racp_rsp_not_supported        = 0x02,
    racp_rsp_number_of_records    = 0x05,
    racp_rsp_code_racp            = 0x06,
    racp_rsp_code_no_record_found = 0x06,

};

SL_ENUM(racp_operator_t){
    racp_operator_null     = 0x00,
    racp_operator_all      = 0x01,
    racp_operator_le       = 0x02,
    racp_operator_ge       = 0x03,
    racp_operator_in_range = 0x04,
    racp_operator_first    = 0x05,
    racp_operator_last     = 0x06,
};

void _app_racp_user_write_request_handler(
    sl_bt_evt_gatt_server_user_write_request_t *req
) {
	app_log_debug(
	    "[app] RACP write request OPCODE:%x." APP_LOG_NL,
	    req->value.data[0]
	);
}
