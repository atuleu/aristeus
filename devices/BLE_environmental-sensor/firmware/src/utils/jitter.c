#include "jitter.h"
#include <sys/reent.h>

#define JITTER_DATA_SIZE 128
#define JITTER_DATA_MASK (JITTER_DATA_SIZE - 1)
static_assert(
    (JITTER_DATA_SIZE & JITTER_DATA_MASK) == 0,
    "JITTER_DATA_SIZE must be a power of two"
);
// pseudo random hardcoded jitter list
static uint8_t jitter_data[JITTER_DATA_SIZE] = {
    224, 76,  118, 250, 98,  163, 13,  236, 54,  225, 73,  178, 167, 147, 83,
    163, 43,  218, 140, 59,  145, 35,  148, 130, 222, 103, 184, 33,  252, 106,
    252, 93,  14,  127, 52,  41,  153, 229, 39,  59,  126, 254, 107, 60,  108,
    251, 81,  24,  39,  126, 249, 118, 237, 179, 176, 174, 207, 251, 65,  5,
    71,  163, 163, 83,  128, 209, 245, 181, 212, 21,  72,  48,  125, 151, 152,
    6,   203, 199, 155, 47,  253, 247, 59,  30,  101, 128, 220, 10,  245, 254,
    239, 71,  196, 32,  191, 216, 237, 154, 47,  136, 69,  4,   220, 205, 46,
    193, 138, 197, 122, 184, 28,  148, 186, 222, 91,  215, 140, 63,  86,  151,
    129, 251, 84,  253, 53,  155, 157, 49,
};

static uint8_t index = 0;

uint8_t jitter() {
	return jitter_data[(++index) & JITTER_DATA_MASK];
}
