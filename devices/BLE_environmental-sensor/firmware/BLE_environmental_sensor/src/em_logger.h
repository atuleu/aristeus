#pragma once

#ifdef __cplusplus
extern "C" {
#endif //__cplusplus

#ifdef PRODUCTION_BUILD
#define em_logger_init()                                                       \
	do {                                                                       \
		/**/                                                                   \
	} while (0)
#define em_logger_print()                                                      \
	do {                                                                       \
	} while (0)
#else

void em_logger_init();

void em_logger_print();
#endif

#ifdef __cplusplus
}
#endif //__cplusplus
