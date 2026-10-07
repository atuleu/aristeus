#pragma once

#if defined(__cplusplus)
extern "C" {
#endif // __cplusplus

#define CORE_DECLARE_IRQ_STATE
#define CORE_ENTER_ATOMIC()
#define CORE_EXIT_ATOMIC()
#define CORE_ATOMIC_SECTION(statements)                                        \
	do {                                                                       \
		statements                                                             \
	} while (0)

#if defined(__cplusplus)
]
#endif // __cplusplus
