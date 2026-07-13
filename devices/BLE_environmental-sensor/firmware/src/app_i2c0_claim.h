#pragma once

#include "sl_i2c.h"
#include "sl_status.h"

#ifdef __cplusplus
extern "C" {
#endif //__cplusplus

sl_status_t app_i2c_claim(sl_i2c_handle_t *instance);

sl_status_t app_i2c_unclaim(sl_i2c_handle_t *instance);

#ifdef __cplusplus
}
#endif //__cplusplus
