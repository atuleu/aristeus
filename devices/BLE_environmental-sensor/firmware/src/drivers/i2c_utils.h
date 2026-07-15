#pragma once

#include "sl_i2c.h"
#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

const char *app_i2c_get_name(sl_i2c_handle_t *instance);

sl_status_t app_i2c_claim(sl_i2c_handle_t *instance);

sl_status_t app_i2c_unclaim(sl_i2c_handle_t *instance);

#ifdef __cplusplus
}
#endif // __cplusplus
