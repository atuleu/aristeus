#pragma once

#include <cstdint>
#include <span>

#include "sl_status.h"

void spiflash_set_memory(std::span<const uint8_t> bytes);

bool spiflash_sleeping();

typedef void (*spiflash_op_callback_t)(sl_status_t status, void *user_data);
