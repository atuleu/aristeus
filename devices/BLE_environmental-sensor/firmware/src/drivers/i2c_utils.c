#include <stdint.h>

#include <bgm220pc22hna.h>
#include <sl_core.h>
#include <sl_status.h>

#include <drivers/i2c_utils.h>

uint8_t i2c_get_index(sl_peripheral_t peripheral) {
	switch (peripheral->base) {
#ifdef I2C0_BASE
	case I2C0_BASE:
		return 0;
#endif // I2C0_BASE
#ifdef I2C1_BASE
	case I2C1_BASE:
		return 1;
#endif // I2C1_BASE
#ifdef I2C2_BASE
	case I2C2_BASE:
		return 2;
#endif // I2C2_BASE
#ifdef I2C3_BASE
	case I2C3_BASE:
		return 3;
#endif // I2C3_BASE
	default:
		return I2C_COUNT;
	}
}

#define ARRAY_SIZE(array) (sizeof(array) / sizeof((array)[0]))

const char *i2c_get_instance_name(sl_i2c_handle_t *instance) {
	if (instance == NULL || instance->i2c_peripheral == NULL) {
		return "I2C<NULL>";
	}
	static const char *names[I2C_COUNT] = {
#ifdef I2C0_BASE
	    "I2C0",
#endif // I2C0_BASE
#ifdef I2C1_BASE
	    "I2C1",
#endif // I2C1_BASE
#ifdef I2C2_BASE
	    "I2C2",
#endif // I2C2_BASE
#ifdef I2C3_BASE
	    "I2C3",
#endif // I2C3_BASE
	};
	uint8_t index = i2c_get_index(instance->i2c_peripheral);
	if (index >= I2C_COUNT) {
		return "I2C<Unknown>";
	}
	return names[index];
}

static volatile bool locked[I2C_COUNT] = {
#ifdef I2C0_BASE
    false,
#endif // I2C0_BASE
#ifdef I2C1_BASE
    false,
#endif // I2C_1BASEW
#ifdef I2C2_BASE
    false,
#endif
#ifdef I2C3_BASE
    false,
#endif
};

sl_status_t i2c_claim_instance(sl_i2c_handle_t *instance) {
	if (instance == NULL) {
		return SL_STATUS_NULL_POINTER;
	}
	if (instance->i2c_peripheral == NULL) {
		return SL_STATUS_INITIALIZATION;
	}
	uint8_t index = i2c_get_index(instance->i2c_peripheral);
	if (index >= I2C_COUNT) {
		return SL_STATUS_INVALID_COUNT;
	}
	sl_status_t status = SL_STATUS_OK;
	CORE_ATOMIC_SECTION(
	    if (locked[index] == false) { locked[index] = true; } else {
		    status = SL_STATUS_BUSY;
	    }
	);
	return status;
}

sl_status_t i2c_unclaim_instance(sl_i2c_handle_t *instance) {
	if (instance == NULL) {
		return SL_STATUS_NULL_POINTER;
	}
	if (instance->i2c_peripheral == NULL) {
		return SL_STATUS_INITIALIZATION;
	}
	uint8_t index = i2c_get_index(instance->i2c_peripheral);
	if (index >= I2C_COUNT) {
		return SL_STATUS_INVALID_COUNT;
	}
	sl_status_t status = SL_STATUS_OK;
	CORE_ATOMIC_SECTION(
	    if (locked[index] == true) { locked[index] = false; } else {
		    status = SL_STATUS_NOT_AVAILABLE;
	    }
	);
	return status;
}
