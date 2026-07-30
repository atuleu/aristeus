#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif //__cplusplus

typedef uint16_t sl_status_t;

#define SL_STATUS_OK                  0x0000
#define SL_STATUS_INVALID_PARAMETER   0x0001
#define SL_STATUS_FULL                0x0002
#define SL_STATUS_INVALID_RANGE       0x0003
#define SL_STATUS_BUSY                0x0004
#define SL_STATUS_FLASH_VERIFY_FAILED 0x0005
#define SL_STATUS_FAIL                0x0006
#define SL_STATUS_EMPTY               0x0007
#define SL_STATUS_INITIALIZATION      0x0008

#ifdef __cplusplus
}
#endif //__cplusplus
