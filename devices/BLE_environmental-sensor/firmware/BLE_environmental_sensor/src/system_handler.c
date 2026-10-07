
#include "em_device.h"
#include <stdio.h>

#define CRASH_MAGIC_KEY 0xDEADBEEF
#define MAX_CRASH_COUNT 3

void HardFault_Handler(void) {
	printf("\r\n================ HARD FAULT DETECTED ================\r\n");

	while (true) {
	}
}
