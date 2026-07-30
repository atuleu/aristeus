#pragma once

#if defined(__cplusplus)
extern "C" {
#endif // __cplusplus

#define APP_LOG_NL "\n"

void app_log_error(const char *fmt, ...);
void app_log_warning(const char *fmt, ...);
void app_log_info(const char *fmt, ...);
void app_log_debug(const char *fmt, ...);
void app_log_trace(const char *fmt, ...);

#if defined(__cplusplus)
}
#endif // __cplusplus
