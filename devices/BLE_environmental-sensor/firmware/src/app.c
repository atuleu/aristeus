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
#include "bt_types.h"
#include "drivers/i2c_schd.h"
#include "drivers/spiflash.h"
#include "em_logger.h"
#include "journal.h"
#include "location.h"
#include "pin_config.h"
#include "sl_spidrv_instances.h"
#include "types.h"
#include "utils/jitter.h"
#include "utils/status.h"
#include <drivers/sht4x.h>
#include <stdio.h>
#include <string.h>
#include <sys/reent.h>

#define BL_LOW_RESSOURCE_THRESHOLD  256
#define BL_HIGH_RESSOURCE_THRESHOLD (1024 + 256)
static_assert(
    BL_HIGH_RESSOURCE_THRESHOLD / 2 > BL_LOW_RESSOURCE_THRESHOLD,
    "weird behavior of the resource threshold stack to avoid"
);

app_handle_t app = {
    .advertising_set_handle = SL_BT_INVALID_ADVERTISING_SET_HANDLE,
    .connection =
        {
            .handle                      = SL_BT_INVALID_CONNECTION_HANDLE,
            .racp_enabled                = false,
            .stream_notification_enabled = false,
        },
    .resource_are_low = false,
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

	adv_data[adv_data_len++] = sizeof(advertisement_data_t);

	advertisement_data_t *app_data =
	    (advertisement_data_t *)&adv_data[adv_data_len];
	adv_data_len += sizeof(advertisement_data_t);

	app_data->ad_type         = 0xFF;   // AD Type: Manufacturer data
	app_data->manufacturer_id = 0xFFFF; // non-registered manufacturer;
	app_data->location        = location_get();
	app_data->battery         = batt_monitor_get_current_level();
	app_data->memory_level    = journal_record_count() >> 8;
	app_data->measurement     = *d;

	app_assert(adv_data_len <= 31, "adv packet too large");

	app_log_debug("[app] new advertised data." APP_LOG_NL);

	return sl_bt_legacy_advertiser_set_data(
	    advertising_set,
	    sl_bt_advertiser_advertising_data_packet,
	    adv_data_len,
	    adv_data
	);
}

sl_status_t _app_start_advertise() {
	sl_status_t status = sl_bt_legacy_advertiser_start(
	    app.advertising_set_handle,
	    sl_bt_legacy_advertiser_connectable
	);
	if (status != SL_STATUS_OK) {
		return status;
	}
	return sl_bt_system_set_lazy_soft_timer(
	    32768 * BT_ADV_PERIOD_MS / 1000,
	    32768 * 20 / 1000,
	    APP_BATT_TIMER_HANDLE,
	    false
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
		sl_bt_system_set_lazy_soft_timer(0, 0, APP_BATT_TIMER_HANDLE, true);
		_app_on_bt_connection_opened(&evt->data.evt_connection_opened);
		sl_bt_connection_set_parameters(
		    app.connection.handle,
		    80 / 1.25,  // 100ms
		    100 / 1.25, // 200ms
		    1,          // latency
		    60,         // 600ms timeout
		    0x0000,
		    0xffff
		);

		break;

	case sl_bt_evt_connection_parameters_id: {
#ifndef PRODUCTION_BUILD
		sl_bt_evt_connection_parameters_t *p =
		    &evt->data.evt_connection_parameters;

		app_log_info(
		    "[app]  connection interval: %dms,latency: %d "
		    "timeout:%dms." APP_LOG_NL,
		    (uint16_t)(p->interval * 1.25f),
		    p->latency,
		    p->timeout * 10
		);
#endif // PRODUCTION_BUILD
		break;
	}

	case sl_bt_evt_connection_closed_id:
		_app_on_bt_connection_closed(&evt->data.evt_connection_closed);
		break;

	case sl_bt_evt_resource_status_id:
		_app_on_ressource_status(&evt->data.evt_resource_status);
		break;

	case sl_bt_evt_gatt_server_characteristic_status_id:
		_app_connection_wd_reset(&app.connection);
		_app_on_gatt_server_characteristic_status(
		    &evt->data.evt_gatt_server_characteristic_status
		);
		break;
	case sl_bt_evt_gatt_server_attribute_value_id:
		_app_connection_wd_reset(&app.connection);
		// do nothing.
		break;
	case sl_bt_evt_gatt_server_user_read_request_id: {
		_app_connection_wd_reset(&app.connection);
		sl_bt_evt_gatt_server_user_read_request_t *req =
		    &evt->data.evt_gatt_server_user_read_request;
		_app_on_gatt_server_user_read_request(req);
		break;
	}

	case sl_bt_evt_gatt_server_user_write_request_id: {
		_app_connection_wd_reset(&app.connection);
		sl_bt_evt_gatt_server_user_write_request_t *req =
		    &evt->data.evt_gatt_server_user_write_request;
		_app_on_gatt_server_user_write_request(req);
		break;
	}

	case sl_bt_evt_system_external_signal_id:
		uint32_t signals = evt->data.evt_system_external_signal.extsignals;
		_app_on_external_signals(signals);
		break;

	case sl_bt_evt_system_soft_timer_id:
		_app_on_soft_timer(&evt->data.evt_system_soft_timer);
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
		    gatt_ecode_succeed,
		    sizeof(time),
		    (const uint8_t *)&time,
		    0
		);
		app_assert_status(sc);
		break;
	}
	case gattdb_hive_location: {
		location_t current_location = location_get();
		sc                          = sl_bt_gatt_server_send_user_read_response(
            req->connection,
            req->characteristic,
            gatt_ecode_succeed,
            sizeof(location_t),
            (const uint8_t *)&current_location,
            0
        );
		app_assert_status(sc);
		break;
	}
	case gattdb_current_pressure: {
		pressure_t p = app_es_current_pressure();
		sc           = sl_bt_gatt_server_send_user_read_response(
            req->connection,
            gattdb_current_pressure,
            gatt_ecode_succeed,
            sizeof(pressure_t),
            (const uint8_t *)&p,
            0
        );
		break;
	}
	default:
		app_log_error("[app] unknown characteristic read request");
		sl_bt_gatt_server_send_user_read_response(
		    req->connection,
		    req->characteristic,
		    gatt_ecode_invalid_handle,
		    0,
		    NULL,
		    0
		);
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
	return app.connection.handle != SL_BT_INVALID_CONNECTION_HANDLE;
}

void _app_connection_reset(app_bt_connection_t *conn) {
	if (conn->handle != SL_BT_INVALID_CONNECTION_HANDLE) {
		_app_connection_wd_stop(&app.connection);
		if (conn->procedure_in_progress == true &&
		    conn->procedure_opcode == racp_opcode_report_records) {
			journal_read_abord();
		}
	}

	conn->handle                      = SL_BT_INVALID_CONNECTION_HANDLE;
	conn->wd_fired                    = false;
	conn->stream_notification_enabled = false;
	conn->racp_enabled                = false;
	conn->inflight_indication         = false;
	conn->procedure_in_progress       = false;
	conn->procedure_opcode            = racp_opcode_reserved;
}

void _app_connection_init(app_bt_connection_t *conn, uint8_t handle) {
	conn->handle                      = handle;
	conn->wd_fired                    = false;
	conn->stream_notification_enabled = false;
	conn->racp_enabled                = false;
	conn->inflight_indication         = false;
	conn->procedure_in_progress       = false;
	conn->procedure_opcode            = racp_opcode_reserved;
	_app_connection_wd_start(&app.connection);
}

void _app_connection_wd_reset(app_bt_connection_t *conn) {
	CORE_ATOMIC_SECTION({
		sl_sleeptimer_restart_timer_ms(
		    &conn->wd,
		    APP_CONNECTION_TIMEOUT_MS,
		    &_app_on_connection_wd_timeout,
		    NULL,
		    0,
		    0
		);
		conn->wd_fired = false;
	});
	app_log_debug("[app] connection WD reset." APP_LOG_NL);
}

void _app_connection_wd_start(app_bt_connection_t *conn) {
	CORE_ATOMIC_SECTION({
		sl_sleeptimer_start_timer_ms(
		    &app.connection.wd,
		    APP_CONNECTION_TIMEOUT_MS,
		    &_app_on_connection_wd_timeout,
		    NULL,
		    0,
		    0
		);
		conn->wd_fired  = false;
		conn->wd_ignore = false;
	});
	app_log_debug("[app] connection WD started." APP_LOG_NL);
}

void _app_connection_wd_stop(app_bt_connection_t *conn) {
	sl_status_t status;
	CORE_ATOMIC_SECTION({
		status = sl_sleeptimer_start_timer_ms(
		    &conn->wd,
		    APP_CONNECTION_TIMEOUT_MS,
		    &_app_on_connection_wd_timeout,
		    NULL,
		    0,
		    0
		);
		app.connection.wd_fired = false;
		if (status != SL_STATUS_OK) {
			conn->wd_ignore = true;
		}
	});
	app_log_debug("[app] connection WD stopped." APP_LOG_NL);
}

void _app_on_connection_wd_timeout(
    sl_sleeptimer_timer_handle_t *timer, void *user_data
) {
	(void)timer;
	(void)user_data;
	CORE_ATOMIC_SECTION({
		if (app.connection.wd_ignore == false) {
			sl_bt_external_signal(APP_CONNECTION_WD_SIGNAL);
			app.connection.wd_fired = true;
		}
	});
}

static inline void _app_on_send_next_signal() {
	if (app.connection.procedure_in_progress == false ||
	    app.connection.procedure_opcode == false ||
	    app.resource_are_low == true) {
		return;
	}
	sl_status_t status = journal_read_resume();
	if (status != SL_STATUS_OK) {
		app_log_error(
		    "[app] could not resume read: %s." APP_LOG_NL,
		    sl_status_get_string(status)
		);

		_app_complete_procedure(racp_rsp_procedure_not_completed);
	}
}

static inline void _app_on_connection_wd_signal() {
	if (app.connection.wd_fired == false || _app_is_connected() == false) {
		return;
	}
	sl_bt_connection_close(app.connection.handle);
	app_log_warning(
	    "[app] closing connection after %d.%03ds of "
	    "inactivity." APP_LOG_NL,
	    APP_CONNECTION_TIMEOUT_MS / 1000,
	    APP_CONNECTION_TIMEOUT_MS % 1000

	);
}

void _app_on_external_signals(uint32_t events) {
	if ((events & APP_SEND_NEXT_SIGNAL) != 0) {
		_app_on_send_next_signal();
	}

	if ((events & APP_CONNECTION_WD_SIGNAL) != 0x00) {
		_app_on_connection_wd_signal();
	}
}

void _app_on_bt_system_boot() {
	sl_status_t status;

	// sets TX power
	int16_t min_power, max_power;
	status = sl_bt_system_set_tx_power(-20, 0, &min_power, &max_power);
	app_assert_status(status);
	app_log_info(
	    "[app] BT power settings min: %d.%ddBm max:%d.%ddBm." APP_LOG_NL,
	    min_power / 10,
	    min_power % 10,
	    max_power / 10,
	    max_power % 10
	);

	// Create an advertising set.
	status = sl_bt_advertiser_create_set(&app.advertising_set_handle);

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
	status = _app_start_advertise();
	app_assert(status);

	// just to ensure we start from unitialized everywhere
	app.connection.handle =
	    SL_BT_INVALID_CONNECTION_HANDLE; // will preempt a spurous WD stop or
	                                     // journal_read_abord.
	_app_connection_reset(&app.connection);
	app_assert_status(status);

	status = sl_bt_resource_set_report_threshold(
	    BL_LOW_RESSOURCE_THRESHOLD,
	    BL_HIGH_RESSOURCE_THRESHOLD
	);
	app_assert_status(status);
}

void _app_on_bt_connection_opened(sl_bt_evt_connection_opened_t *evt) {
	app_log_info(
	    "[app] connected with %02X:%02X:%02X:%02X:%02X:%02X." APP_LOG_NL,
	    evt->address.addr[0],
	    evt->address.addr[1],
	    evt->address.addr[2],
	    evt->address.addr[3],
	    evt->address.addr[4],
	    evt->address.addr[5]
	);
	_app_connection_init(&app.connection, evt->connection);
}

void _app_on_bt_connection_closed(sl_bt_evt_connection_closed_t *evt) {
	(void)evt;
	_app_connection_reset(&app.connection);

	app_log_info("[app] disconnected." APP_LOG_NL);
	// Generate data for advertising

	sl_status_t status = app_set_legacy_advertiser_data(
	    app.advertising_set_handle,
	    app_es_current_data()
	);

	app_assert_status(status);

	// Restart advertising after client has disconnected.
	status = _app_start_advertise();
	app_assert_status(status);
}

void _app_on_gatt_server_user_write_request(
    sl_bt_evt_gatt_server_user_write_request_t *req
) {
	app_log_debug(
	    "[app] GATT user write request: %d." APP_LOG_NL,
	    req->characteristic
	);
	gatt_ecode_t err = gatt_ecode_succeed;
	switch (req->characteristic) {
	case gattdb_current_time_epoch: {
		sl_sleeptimer_timestamp_t now;
		if (req->value.len != sizeof(sl_sleeptimer_timestamp_t)) {
			err = gatt_ecode_invalid_attribute_length;
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
			err = gatt_ecode_invalid_attribute_length;
		} else {
			memcpy(&new_location, req->value.data, sizeof(location_t));
			sl_status_t sc = location_set(new_location);
			if (sc != SL_STATUS_OK) {
				err = gatt_ecode_write_request_rejected;
			}
		}

		sl_bt_gatt_server_send_user_write_response(
		    req->connection,
		    req->characteristic,
		    err
		);
		break;
	}
	case gattdb_current_pressure: {
		pressure_t new_pressure;
		err = gatt_ecode_succeed;
		if (req->value.len != sizeof(pressure_t)) {
			err = gatt_ecode_invalid_attribute_length;
		} else {
			memcpy(&new_pressure, req->value.data, sizeof(pressure_t));
			sl_status_t sc = app_es_tare_pressure(new_pressure);
			if (sc != SL_STATUS_OK) {
				err = gatt_ecode_write_request_rejected;
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
		sl_bt_gatt_server_send_user_write_response(
		    req->connection,
		    req->characteristic,
		    gatt_ecode_invalid_handle
		);
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
		_app_racp_indication_handler(evt);
		break;
	default:
		app_log_warning(
		    "[app] unknwon GATT characteristic status %d." APP_LOG_NL,
		    evt->characteristic
		);
	}
}

void _app_stream_data_notification_handler(
    sl_bt_evt_gatt_server_characteristic_status_t *status
) {
	app_log_debug(
	    "[app] stream data notification handler %02x %02x." APP_LOG_NL,
	    status->status_flags,
	    status->client_config_flags
	);

	if ((status->status_flags & sl_bt_gatt_server_client_config) == 0x00) {
		return;
	}

	// we set the configuration from the client. Only interested on indications.
	app.connection.stream_notification_enabled =
	    (status->client_config_flags & sl_bt_gatt_server_notification) != 0x00;

	app_log_info(
	    "[app] stream data notification enabled: %s" APP_LOG_NL,
	    app.connection.stream_notification_enabled ? "true" : "false"
	);
}

void _app_racp_indication_handler(
    sl_bt_evt_gatt_server_characteristic_status_t *status
) {
	app_log_debug(
	    "[app] RACP notification handler %02x %02x." APP_LOG_NL,
	    status->status_flags,
	    status->client_config_flags
	);

	if ((status->status_flags & sl_bt_gatt_server_confirmation) ==
	    sl_bt_gatt_server_confirmation) {
		_app_on_indication_confirmation();
	}

	if ((status->status_flags & sl_bt_gatt_server_client_config) == 0x00) {
		return;
	}

	// we set the configuration from the client. Only interested on indications.
	app.connection.racp_enabled =
	    (status->client_config_flags & sl_bt_gatt_server_indication) != 0x00;

	app_log_info(
	    "[app] RACP indication enabled: %s" APP_LOG_NL,
	    app.connection.racp_enabled ? "true" : "false"
	);
}

void _app_racp_user_write_request_handler(
    sl_bt_evt_gatt_server_user_write_request_t *req
) {
	racp_opcode_t opcode = req->value.data[0];
#ifndef PRODUCTION_BUILD
	app_log_debug(
	    "[app] RACP write request OPCODE:%02X len=%d data=",
	    opcode,
	    req->value.len
	);
	const char *sep = "{ ";
	for (int i = 1; i < req->value.len; ++i) {
		app_log_append("%s%02X", sep, req->value.data[i]);
		sep = ", ";
	}
	app_log_append(" }" APP_LOG_NL);
#endif // PRODUCTION_BUILD
	if (req->value.len < 2) {
		sl_bt_gatt_server_send_user_write_response(
		    req->connection,
		    gattdb_record_access_control_point,
		    gatt_ecode_invalid_attribute_length
		);
		return;
	}

	if (app.connection.stream_notification_enabled == false ||
	    app.connection.racp_enabled == false) {
		app_log_warning(
		    "[app] client did not enable indication for stream data or "
		    "RACP." APP_LOG_NL
		);
		sl_bt_gatt_server_send_user_write_response(
		    req->connection,
		    gattdb_record_access_control_point,
		    gatt_ecode_not_indicated
		);
		return;
	}

	if (app.connection.procedure_in_progress == true) {
		sl_bt_gatt_server_send_user_write_response(
		    req->connection,
		    gattdb_record_access_control_point,
		    gatt_ecode_procedure_in_progress
		);
		return;
	}
	sl_bt_gatt_server_send_user_write_response(
	    req->connection,
	    gattdb_record_access_control_point,
	    gatt_ecode_succeed
	);

	switch (opcode) {
	case racp_opcode_report_records:
		_app_racp_range_operation(req);
		break;
	case racp_opcode_delete_records:
		_app_racp_delete_records(req);
		break;
	case racp_opcode_report_number:
		_app_racp_range_operation(req);
		break;
	case racp_opcode_abort_operation:
		// normally it is a mandatory one, but we do not implement it so we
		// 'accept' but fail it immediatly.
		_app_racp_send_response(opcode, racp_rsp_procedure_not_completed);
		break;

	case racp_opcode_reserved:
	default:
		app_log_error("[app] unsupported opcode %02X." APP_LOG_NL, opcode);
		_app_racp_send_response(opcode, racp_rsp_opcode_not_supported);
	};
}

void _app_racp_send_number_of_records(uint16_t number) {
	uint8_t buffer[4] = {
	    racp_opcode_number_rsp,
	    racp_operator_null,
	    number & 0xff,
	    number >> 8,
	};
	sl_status_t status = sl_bt_gatt_server_send_indication(
	    app.connection.handle,
	    gattdb_record_access_control_point,
	    sizeof(buffer),
	    buffer
	);
	if (status != SL_STATUS_OK) {
		app_log_error(
		    "[app] cannot send RACP number indication: %s." APP_LOG_NL,
		    sl_status_get_string(status)
		);
	} else {
		app.connection.inflight_indication = true;
	}
}

void _app_racp_send_response(racp_opcode_t opcode, racp_rsp_t response_code) {
	uint8_t buffer[4] =
	    {racp_opcode_rsp, racp_operator_null, opcode, response_code};
	sl_status_t status = sl_bt_gatt_server_send_indication(
	    app.connection.handle,
	    gattdb_record_access_control_point,
	    sizeof(buffer),
	    buffer
	);
	if (status != SL_STATUS_OK) {
		app_log_error(
		    "[app] cannot send RACP response indication: %s." APP_LOG_NL,
		    sl_status_get_string(status)
		);
	} else {
		app.connection.inflight_indication = true;
	}
}

void _app_on_indication_confirmation() {
	if (app.connection.inflight_indication == false) {
		app_log_error("[app] spurious indication confirmation." APP_LOG_NL);
		return;
	}
	app.connection.inflight_indication = false;
}

void _app_racp_range_operation(sl_bt_evt_gatt_server_user_write_request_t *req
) {
	racp_opcode_t   opcode = req->value.data[0];
	racp_operator_t operator= req->value.data[1];
	switch (operator) {
	case racp_operator_null:
		_app_racp_send_response(opcode, racp_rsp_invalid_operator);
		return;
	case racp_operator_all: {
		if (req->value.len != 2) {
			_app_racp_send_response(opcode, racp_rsp_invalid_operand);
			return;
		}
		_app_find_journal_range_inclusive(opcode, 0, UINT32_MAX);
		return;
	}
	case racp_operator_le:
	case racp_operator_ge: {
		if (req->value.len != 7 || req->value.data[2] != 0x01) {
			_app_racp_send_response(opcode, racp_rsp_invalid_operand);
			return;
		}
		sl_sleeptimer_timestamp_t operand =
		    ((uint32_t)req->value.data[3] << 0) |
		    ((uint32_t)req->value.data[4] << 8) |
		    ((uint32_t)req->value.data[5] << 16) |
		    ((uint32_t)req->value.data[6] << 24);
		if (opcode == racp_operator_ge) {
			_app_find_journal_range_inclusive(opcode, operand, UINT32_MAX);
		} else {
			_app_find_journal_range_inclusive(opcode, 0, operand);
		}
		return;
	}
	case racp_operator_in_range: {
		if (req->value.len != 11 || req->value.data[2] != 0x01) {
			_app_racp_send_response(opcode, racp_rsp_invalid_operand);
			return;
		}

		sl_sleeptimer_timestamp_t low_operand =
		    ((uint32_t)req->value.data[3] << 0) |
		    ((uint32_t)req->value.data[4] << 8) |
		    ((uint32_t)req->value.data[5] << 16) |
		    ((uint32_t)req->value.data[6] << 24);

		sl_sleeptimer_timestamp_t high_operand =
		    ((uint32_t)req->value.data[7] << 0) |
		    ((uint32_t)req->value.data[8] << 8) |
		    ((uint32_t)req->value.data[9] << 16) |
		    ((uint32_t)req->value.data[10] << 24);

		_app_find_journal_range_inclusive(opcode, low_operand, high_operand);
		return;
	}
	default:
		_app_racp_send_response(opcode, racp_rsp_operator_not_supported);
	}
}

void _app_racp_delete_records(sl_bt_evt_gatt_server_user_write_request_t *req) {
	racp_opcode_t   opcode = req->value.data[0];
	racp_operator_t operator= req->value.data[1];
	switch (operator) {
	case racp_operator_null:
		_app_racp_send_response(opcode, racp_rsp_invalid_operator);
		break;
	case racp_operator_all: {
		if (req->value.len != 2) {
			_app_racp_send_response(opcode, racp_rsp_invalid_operand);
			return;
		}
		sl_status_t status = journal_erase();
		if (status != SL_STATUS_OK) {
			_app_racp_send_response(
			    opcode,
			    status == SL_STATUS_BUSY ? racp_rsp_server_busy
			                             : racp_rsp_procedure_not_completed
			);
			return;
		}

		_app_racp_send_response(opcode, racp_rsp_success);
		break;
	}
	default:
		_app_racp_send_response(opcode, racp_rsp_operator_not_supported);
	}
}

void _app_procedure_action(
    sl_status_t status, journal_index_t start, journal_index_t end
) {
	racp_opcode_t opcode = app.connection.procedure_opcode;
	app_log_debug(
	    "[app] running RACP procedure opcode=%02X, range_find_status=%s "
	    "start=%ld end=%ld." APP_LOG_NL,
	    opcode,
	    sl_status_get_string(status),
	    start,
	    end
	);

	switch (opcode) {
	case racp_opcode_report_records:
		_app_readout_range(status, start, end);
		break;
	case racp_opcode_report_number:
		_app_count_range(status, start, end);
		break;
	default:
		app_log_error(
		    "[app] spurious procedure action for opcode=%02X." APP_LOG_NL,
		    opcode
		);
		_app_racp_send_response(opcode, racp_rsp_procedure_not_completed);
	}
}

void _app_readout_range(
    sl_status_t status, journal_index_t start, journal_index_t end
) {
	if (status != SL_STATUS_OK || end <= start) {
		app_log_warning("[app] nothing to read." APP_LOG_NL);
		_app_complete_procedure(racp_rsp_no_record_found);
		return;
	}

	if (status != SL_STATUS_OK) {
		app_log_warning(
		    "[app] could not change connection parameters: %s." APP_LOG_NL,
		    sl_status_get_string(status)
		);
	}

	app_log_info("[app] starting to read." APP_LOG_NL);
	status = journal_read(start, end, &_app_on_record_read, NULL);
	if (status != SL_STATUS_OK) {
		_app_complete_procedure(racp_rsp_procedure_not_completed);
	}
}

void _app_count_range(
    sl_status_t status, journal_index_t start, journal_index_t end
) {
	if (status != SL_STATUS_OK || end < start) {
		app_log_warning("[app] report no record found." APP_LOG_NL);
		_app_complete_procedure(racp_rsp_procedure_not_completed);
		return;
	}

	app_log_info("[app] reporting %ld records." APP_LOG_NL, end - start);

	app.connection.procedure_in_progress = false;
	app.connection.procedure_opcode      = racp_opcode_reserved;
	_app_racp_send_number_of_records(end - start);
}

void _app_find_journal_range_inclusive(
    racp_opcode_t             opcode,
    sl_sleeptimer_timestamp_t low,
    sl_sleeptimer_timestamp_t high
) {

	app.connection.procedure_in_progress = true;
	_app_connection_wd_stop(&app.connection);
	app.connection.procedure_opcode       = opcode;
	app.connection.procedure_start_target = (low > 0) ? low - 1 : 0;
	app.connection.procedure_end_value    = JOURNAL_INDEX_NPOS;

	// first we search for high
	sl_status_t status =
	    journal_find_last_before(high, &_app_on_find_last_before, NULL);
	if (status == SL_STATUS_EMPTY || status == SL_STATUS_INVALID_RANGE) {
		// early failure, either empty journal, or all value are bigger than
		// high.
		_app_procedure_action(
		    SL_STATUS_OK,
		    JOURNAL_INDEX_NPOS,
		    JOURNAL_INDEX_NPOS
		);
		return;
	}

	if (status != SL_STATUS_OK) {
		app_log_warning(
		    "[app] could not run RACP procedure (OpCode:%d): %s." APP_LOG_NL,
		    opcode,
		    sl_status_get_string(status)
		);
		_app_complete_procedure(racp_rsp_procedure_not_completed);
		return;
	}
}

void _app_on_find_last_before(
    sl_status_t               status,
    journal_index_t           idx,
    sl_sleeptimer_timestamp_t ts,
    void                     *user_data
) {
	(void)user_data;
	(void)ts;
	if (status != SL_STATUS_OK || idx == JOURNAL_INDEX_NPOS) {
		if (app.connection.procedure_end_value == JOURNAL_INDEX_NPOS) {
			// we cannot find the high, bound failure it
			_app_procedure_action(
			    status,
			    JOURNAL_INDEX_NPOS,
			    JOURNAL_INDEX_NPOS
			);
		} else {
			// the journal could not find the value
			_app_procedure_action(
			    status,
			    JOURNAL_INDEX_NPOS,
			    app.connection.procedure_end_value
			);
		}
		return;
	}

	if (app.connection.procedure_end_value != JOURNAL_INDEX_NPOS) {
		// start search case, successful.
		_app_procedure_action(
		    SL_STATUS_OK,
		    idx,
		    app.connection.procedure_end_value
		);
		return;
	}

	// end search case; successfull, the end of the range is idx+1!!!!
	app.connection.procedure_end_value = idx + 1;

	status = journal_find_last_before(
	    app.connection.procedure_start_target,
	    &_app_on_find_last_before,
	    NULL
	);

	if (status == SL_STATUS_OK) {
		return;
	}

	if (status == SL_STATUS_EMPTY || status == SL_STATUS_INVALID_RANGE) {
		_app_procedure_action(
		    SL_STATUS_OK,
		    0,
		    app.connection.procedure_end_value
		);
		return;
	}

	// we must fail it, as we will not be called back.
	_app_procedure_action(status, JOURNAL_INDEX_NPOS, JOURNAL_INDEX_NPOS);
}

journal_read_next_operation_t _app_on_record_read(
    sl_status_t status, const data_point_t *point, void *user_data
) {
	(void)user_data;

	if (status != SL_STATUS_OK) {
		app_log_error(
		    "[app] record read issue: %s." APP_LOG_NL,
		    sl_status_get_string(status)
		);
		_app_complete_procedure(racp_rsp_procedure_not_completed);
		return journal_read_discard;
	}

	if (point == NULL) {
		app_log_info("[app] End Of Record." APP_LOG_NL);
		// we are at the last record
		_app_complete_procedure(racp_rsp_success);
		return journal_read_continue;
	}
	status = sl_bt_gatt_server_send_notification(
	    app.connection.handle,
	    gattdb_stream_data,
	    sizeof(data_point_t),
	    (const uint8_t *)point
	);
	if (status != SL_STATUS_OK) {
		app_log_error(
		    "[app] could not stream data point ts=%ld notification: "
		    "%s." APP_LOG_NL,
		    point->date,
		    sl_status_get_string(status)
		);
		app.connection.inflight_indication = false;
		_app_complete_procedure(racp_rsp_procedure_not_completed);
		return journal_read_discard;
	}

	// we mark that we have more to send
	sl_bt_external_signal(APP_SEND_NEXT_SIGNAL);

	return journal_read_pause;
}

void _app_on_ressource_status(sl_bt_evt_resource_status_t *evt) {
	if (evt->free_bytes <= BL_LOW_RESSOURCE_THRESHOLD) {
		app_log_warning(
		    "[app] BT stack low on resource (%ldB)." APP_LOG_NL,
		    evt->free_bytes
		);
		app.resource_are_low = true;

		return;
	}
	app_log_info(
	    "[app] BT stack high on resources %ldB." APP_LOG_NL,
	    evt->free_bytes
	);
	app.resource_are_low = false;
	// we continue to send if needed on high resource available
	sl_bt_external_signal(APP_SEND_NEXT_SIGNAL);
}

void _app_complete_procedure(racp_rsp_t rsp_code) {
	app.connection.procedure_in_progress = false;
	racp_opcode_t opcode                 = app.connection.procedure_opcode;
	app.connection.procedure_opcode      = racp_opcode_reserved;
	_app_racp_send_response(opcode, rsp_code);
	// we re-start the WD at the end of the procedure.
	_app_connection_wd_start(&app.connection);
}

void _app_on_soft_timer(sl_bt_evt_system_soft_timer_t *evt) {
	switch (evt->handle) {
	case APP_BATT_TIMER_HANDLE:
		batt_monitor_start_open_measurement(
		    sl_sleeptimer_ms_to_tick(17) + jitter()
		);
		break;
	default:
		app_log_warning(
		    "[app] spurious timer event HANDLE=%d." APP_LOG_NL,
		    evt->handle
		);
	}
}
