#include "sl_status.h"

#include <array>
#include <cstdio>

extern "C" {

const char *sl_status_get_string(sl_status_t status) {
	std::array<const char *, 10> known = {
	    "SL_STATUS_OK",
	    "SL_STATUS_INVALID_PARAMETER",
	    "SL_STATUS_FULL",
	    "SL_STATUS_INVALID_RANGE",
	    "SL_STATUS_BUSY",
	    "SL_STATUS_FLASH_VERIFY_FAILED",
	    "SL_STATUS_FAIL               ",
	    "SL_STATUS_EMPTY              ",
	    "SL_STATUS_INITIALIZATION     ",
	    "SL_STATUS_ALREADY_INITIALIZED",
	};
	if (status < known.size()) {
		return known[status];
	}
	static char   buffer[1024];
	static size_t last    = 0;
	size_t        current = last;
	last += snprintf(&buffer[last], 128, "SL_STATUS_0x%04X", status);
	if (last + 128 > 1024) {
		last = 0;
	}
	return &buffer[current];
}
}
