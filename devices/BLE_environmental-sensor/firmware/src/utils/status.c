#include "status.h"
#include "sl_status.h"
#define SL_STATUS_STR_MAX_LENGTH  128
#define SL_STATUS_STR_BUFFER_SIZE 1024

const char *sl_status_get_string(sl_status_t status) {
	static char     buffer[SL_STATUS_STR_BUFFER_SIZE];
	static uint32_t last    = 0;
	uint32_t        current = last;

	last +=
	    sl_status_get_string_n(status, &buffer[last], SL_STATUS_STR_MAX_LENGTH);
	if (last + SL_STATUS_STR_MAX_LENGTH > SL_STATUS_STR_BUFFER_SIZE) {
		last = 0;
	}
	return &buffer[current];
}
