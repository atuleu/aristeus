#include "app_sht4x.h"
#include "sl_power_manager.h"
#include "sl_status.h"
#include "types.h"

volatile bool                      blocking_complete;
static app_sht4x_blocking_result_t blocking_result;

void app_sht4x_rsn_blocking_callback(sl_status_t s, uint32_t sn) {
	blocking_result.status             = s;
	blocking_result.data.serial_number = sn;
	__DMB();
	blocking_complete = true;
}

void app_sht4x_rth_blocking_callback(
    sl_status_t s, temperature_t temperature, humidity_t humidity
) {
	blocking_result.status                      = s;
	blocking_result.data.th_readout.temperature = temperature;
	blocking_result.data.th_readout.humidity    = humidity;
	__DMB();
	blocking_complete = true;
}

app_sht4x_blocking_result_t app_sht4x_read_serial_number_blocking() {
	blocking_complete = false;
	__DMB();
	blocking_result.status =
	    app_sht4x_read_serial_number(&app_sht4x_rsn_blocking_callback);

	if (blocking_result.status != SL_STATUS_OK) {
		return blocking_result;
	}
	while (blocking_complete == false) {
		sl_power_manager_sleep();
	}
	return blocking_result;
}

app_sht4x_blocking_result_t
app_sht4x_read_temperature_blocking(app_sht4x_command_e cmd) {
	blocking_complete = false;
	__DMB();
	blocking_result.status =
	    app_sht4x_read_temperature(cmd, &app_sht4x_rth_blocking_callback);

	if (blocking_result.status != SL_STATUS_OK) {
		return blocking_result;
	}
	while (blocking_complete == false) {
		sl_power_manager_sleep();
	}

	return blocking_result;
}
