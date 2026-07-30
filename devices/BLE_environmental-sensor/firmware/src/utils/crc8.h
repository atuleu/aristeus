#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

uint8_t CRC8_AppendByte(uint8_t current, uint8_t generator, uint8_t value);

#ifdef __cplusplus
}
#endif // __cplusplus
