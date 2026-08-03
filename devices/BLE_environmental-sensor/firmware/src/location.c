#include "location.h"
#include "app_log.h"
#include "nvm3_default.h"

#define NVM3_LOCATION_KEY (NVM3_KEY_MIN + 0x00010)

static location_t location = {.hive_id = 0, .placement = placement_general};
static bool       read     = false;

location_t location_get() {
	if (read == false) {
		sl_status_t status = nvm3_readData(
		    nvm3_defaultHandle,
		    NVM3_LOCATION_KEY,
		    &location,
		    sizeof(location_t)
		);
		if (status != SL_STATUS_OK) {
			app_log_error(
			    "[location] could not read location: 0x%04lX." APP_LOG_NL,
			    status
			);
		}
		if (status == SL_STATUS_OK || status == SL_STATUS_NOT_FOUND) {
			read = true;
		}
	}
	return location;
}

sl_status_t location_set(location_t value) {
	location           = value;
	sl_status_t status = nvm3_writeData(
	    nvm3_defaultHandle,
	    NVM3_LOCATION_KEY,
	    &value,
	    sizeof(location_t)
	);

	if (status != SL_STATUS_OK) {
		app_log_error(
		    "[location] could not set location : 0x%04lx." APP_LOG_NL,
		    status
		);
	} else {
		read = true;
	}
	return status;
}
