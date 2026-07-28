#pragma once

#if defined(__cplusplus)
extern "C" {
#endif // __cplusplus

#define SL_ENUM(name)                                                          \
	typedef uint8_t name;                                                      \
	enum name##_enum
#define SL_ENUM_GENERIC(name, type)                                            \
	typedef type name;                                                         \
	enum name##_enum

#if defined(__cplusplus)
]
#endif // __cplusplus
