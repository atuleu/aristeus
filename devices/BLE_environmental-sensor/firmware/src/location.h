#pragma once

#include <stdint.h>

#include "sl_enum.h"
#include "sl_status.h"

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

location_t location_get();

sl_status_t location_set(location_t location);

#ifdef __cplusplus
}
#endif // __cplusplus
