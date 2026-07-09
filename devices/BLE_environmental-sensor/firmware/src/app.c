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
#include "app_assert.h"
#include "app_log.h"
#include "gatt_db.h"
#include "sl_bt_api.h"
#include "sl_main_init.h"
#include "sl_sleeptimer.h"
#include <stdint.h>
// The advertising set handle allocated from Bluetooth stack.
static uint8_t                      advertising_set_handle = 0xff;
static sl_sleeptimer_timer_handle_t alive_timer;

void great_callback(sl_sleeptimer_timer_handle_t *handle, void *udata) {
	(void)handle;
	(void)udata;
	app_log_info("alive" APP_LOG_NL);
}

// Application Init.
void app_init(void) {
	/////////////////////////////////////////////////////////////////////////////
	// Put your additional application init code here! // This is called once
	// during start-up.                                    //
	/////////////////////////////////////////////////////////////////////////////
	sl_sleeptimer_start_periodic_timer_ms(
	    &alive_timer,
	    1000,
	    &great_callback,
	    (void *)NULL,
	    0,
	    0
	);
}

// Application Process Action.
void app_process_action(void) {
	if (app_is_process_required() == false) {
		return;
	}
	/////////////////////////////////////////////////////////////////////////////
	// Put your additional application code here! This is will run each time
	// app_proceed() is called.
	//
	// Do not call blocking functions from here!
	/////////////////////////////////////////////////////////////////////////////
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
		sc = sl_bt_advertiser_create_set(&advertising_set_handle);
		app_assert_status(sc);

		// Generate data for advertising
		sc = sl_bt_legacy_advertiser_generate_data(
		    advertising_set_handle,
		    sl_bt_advertiser_general_discoverable
		);
		app_assert_status(sc);

		// Set advertising interval to 100ms.
		sc = sl_bt_advertiser_set_timing(
		    advertising_set_handle,
		    160, // min. adv. interval (milliseconds / 1.6)
		    160, // max. adv. interval (milliseconds / 1.6)
		    0,   // adv. duration
		    0
		); // max. num. adv. events
		app_assert_status(sc);
		// Start advertising and enable connections.
		sc = sl_bt_legacy_advertiser_start(
		    advertising_set_handle,
		    sl_bt_legacy_advertiser_connectable
		);
		app_assert_status(sc);
		break;

	// -------------------------------
	// This event indicates that a new connection was opened.
	case sl_bt_evt_connection_opened_id:
		app_log_info("connected");
		break;

	// -------------------------------
	// This event indicates that a connection was closed.
	case sl_bt_evt_connection_closed_id:
		app_log_info("disconnected");
		// Generate data for advertising
		sc = sl_bt_legacy_advertiser_generate_data(
		    advertising_set_handle,
		    sl_bt_advertiser_general_discoverable
		);
		app_assert_status(sc);

		// Restart advertising after client has disconnected.
		sc = sl_bt_legacy_advertiser_start(
		    advertising_set_handle,
		    sl_bt_legacy_advertiser_connectable
		);
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
