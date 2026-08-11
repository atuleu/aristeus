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

#include "bt_types.h"
#include "drivers/i2c_schd.h"
#include "journal.h"
#include "sl_bt_api.h"
#include "sl_sleeptimer.h"
#include "sl_status.h"
#include "types.h"
#include <stdbool.h>
#include <stdint.h>

#define JOURNAL_MINIMUM_DATE                                                   \
	1767225600 // correspond to 2026-01-01T00:00:00.000Z

#ifdef PRODUCTION_BUILD
#define SENSOR_READOUT_PERIOD_S   60
#define BT_ADV_PERIOD_MS          5000
#define APP_CONNECTION_TIMEOUT_MS 10 * 1000
#else
#define SENSOR_READOUT_PERIOD_S   10
#define BT_ADV_PERIOD_MS          1000
#define APP_CONNECTION_TIMEOUT_MS 60 * 1000
#endif

#define APP_SEND_NEXT_SIGNAL      0x01
#define APP_CONNECTION_WD_SIGNAL  0x02
#define APP_DEBUG_RESOURCE_SIGNAL 0x04

#define APP_BATT_TIMER_HANDLE 1

typedef struct app_bt_connection {
	uint8_t                      handle;
	sl_sleeptimer_timer_handle_t wd;
	volatile bool                wd_fired;
	bool                         wd_ignore;

	bool                      stream_notification_enabled;
	bool                      racp_enabled;
	bool                      inflight_indication;
	bool                      procedure_in_progress;
	sl_sleeptimer_timestamp_t procedure_start_target;
	journal_index_t           procedure_end_value;
	racp_opcode_t             procedure_opcode;

} app_bt_connection_t;

void _app_connection_reset(app_bt_connection_t *conn);
void _app_connection_init(app_bt_connection_t *conn, uint8_t handle);
void _app_connection_wd_start(app_bt_connection_t *conn);
void _app_connection_wd_stop(app_bt_connection_t *conn);
void _app_connection_wd_reset(app_bt_connection_t *conn);

void _app_on_connection_wd_timeout(
    sl_sleeptimer_timer_handle_t *timer, void *user_data
);

typedef struct app_handle {
	uint8_t advertising_set_handle;

	i2c_schd_handle_t i2c0;

	app_bt_connection_t connection;
	bool                resource_are_low;
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

bool app_is_ok_to_sleep(void);
bool _app_is_connected();
void app_init();

sl_status_t
app_set_legacy_advertiser_data(uint8_t advertising_set, const data_point_t *d);

sl_status_t _app_start_advertise();

void _app_on_ressource_status(sl_bt_evt_resource_status_t *evt);
void _app_on_gatt_server_characteristic_status(
    sl_bt_evt_gatt_server_characteristic_status_t *evt
);
void _app_on_gatt_server_user_read_request(
    sl_bt_evt_gatt_server_user_read_request_t *req
);
void _app_on_gatt_server_user_write_request(
    sl_bt_evt_gatt_server_user_write_request_t *req
);
void _app_on_external_signals(uint32_t events);

void _app_on_es_readout(sl_status_t status, const data_point_t *point);

void _app_on_bt_system_boot();
void _app_on_bt_connection_opened(sl_bt_evt_connection_opened_t *evt);
void _app_on_bt_connection_closed(sl_bt_evt_connection_closed_t *evt);

void _app_racp_user_write_request_handler(
    sl_bt_evt_gatt_server_user_write_request_t *req
);

void _app_stream_data_notification_handler(
    sl_bt_evt_gatt_server_characteristic_status_t *evt
);
void _app_racp_indication_handler(
    sl_bt_evt_gatt_server_characteristic_status_t *evt
);

void _app_racp_range_operation(sl_bt_evt_gatt_server_user_write_request_t *req);

void _app_racp_delete_records(sl_bt_evt_gatt_server_user_write_request_t *req);

void _app_racp_send_number_of_records(uint16_t number);
void _app_racp_send_response(racp_opcode_t opcode, racp_rsp_t error);

void _app_on_indication_confirmation();

void _app_find_journal_range_inclusive(
    racp_opcode_t             opcode,
    sl_sleeptimer_timestamp_t low,
    sl_sleeptimer_timestamp_t high
);

void _app_procedure_action(
    sl_status_t status, journal_index_t start, journal_index_t end
);

void _app_complete_procedure(racp_rsp_t rsp_code);

void _app_readout_range(
    sl_status_t status, journal_index_t start, journal_index_t end
);
void _app_count_range(
    sl_status_t status, journal_index_t start, journal_index_t end
);
void _app_on_find_last_before(
    sl_status_t               status,
    journal_index_t           idx,
    sl_sleeptimer_timestamp_t ts,
    void                     *user_data
);

journal_read_next_operation_t _app_on_record_read(
    sl_status_t status, const data_point_t *point, void *user_data
);

void _app_on_soft_timer(sl_bt_evt_system_soft_timer_t *evt);
