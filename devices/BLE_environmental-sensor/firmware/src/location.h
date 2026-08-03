#pragma once

#include <stdint.h>

#include "sl_enum.h"
#include "sl_status.h"

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

SL_ENUM(placement_t){
    placement_general = 0x00,
    placement_outside = 0x01,
    placement_inside  = 0x02,
    placement_top     = 0x04,
    placement_front   = 0x08,
    placement_left    = 0x10,
};

typedef struct __attribute__((packed)) location {
	uint8_t     hive_id;
	placement_t placement;
} location_t;

static_assert(sizeof(location_t) == 2, "Invalid location size");

location_t location_get();

sl_status_t location_set(location_t location);

#ifdef __cplusplus
}
#endif // __cplusplus
