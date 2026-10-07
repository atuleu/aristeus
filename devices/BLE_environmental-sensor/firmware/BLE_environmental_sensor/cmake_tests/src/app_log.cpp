#include "app_log.h"

#include <cstdarg>
#include <cstdio>

extern "C" {

void app_log_error(const char *fmt, ...) {
	std::fputs("[E] ", stdout);
	va_list args;
	va_start(args, fmt);
	std::vprintf(fmt, args);
	va_end(args);
}

void app_log_warning(const char *fmt, ...) {
	std::fputs("[W] ", stdout);
	std::va_list args;
	va_start(args, fmt);
	std::vprintf(fmt, args);
	va_end(args);
}

void app_log_info(const char *fmt, ...) {
	std::fputs("[I] ", stdout);
	std::va_list args;
	va_start(args, fmt);
	std::vprintf(fmt, args);
	va_end(args);
}

void app_log_debug(const char *fmt, ...) {
	std::fputs("[D] ", stdout);
	std::va_list args;
	va_start(args, fmt);
	std::vprintf(fmt, args);
	va_end(args);
}

void app_log_trace(const char *fmt, ...) {
	std::fputs("[T] ", stdout);
	std::va_list args;
	va_start(args, fmt);
	std::vprintf(fmt, args);
	va_end(args);
}
}
