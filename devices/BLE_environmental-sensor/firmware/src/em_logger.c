#include "em_logger.h"
#include "app_log.h"
#include "sl_core.h"
#include "sl_power_manager.h"
#include "sl_power_manager_debug.h"
#include "sl_sleeptimer.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef struct em_logger {
	volatile uint64_t last_transition;

	volatile uint64_t count_ticks[5];

	sl_power_manager_em_transition_event_handle_t event_handle;
	sl_power_manager_em_transition_event_info_t   event_info;
} em_logger_t;

void _em_logger_on_pm_event(
    sl_power_manager_em_t from, sl_power_manager_em_t to
);

static em_logger_t el = {
    .event_info =
        {
            .event_mask = SL_POWER_MANAGER_EVENT_TRANSITION_ENTERING_EM0 |
                          SL_POWER_MANAGER_EVENT_TRANSITION_LEAVING_EM0 |
                          SL_POWER_MANAGER_EVENT_TRANSITION_ENTERING_EM1 |
                          SL_POWER_MANAGER_EVENT_TRANSITION_LEAVING_EM1 |
                          SL_POWER_MANAGER_EVENT_TRANSITION_ENTERING_EM2 |
                          SL_POWER_MANAGER_EVENT_TRANSITION_LEAVING_EM2,
            .on_event = &_em_logger_on_pm_event,
        },
};

void em_logger_print() {
	uint64_t count_ticks[5];
	CORE_ATOMIC_SECTION({
		memcpy(count_ticks, (void *)el.count_ticks, sizeof(count_ticks));
	});
	uint32_t freq  = sl_sleeptimer_get_timer_frequency();
	float    total = 0;
	for (uint8_t i = 0; i < 5; ++i) {
		total += count_ticks[i];
	}

	printf("------------------------------------------" APP_LOG_NL);
	printf("| EM Statistics" APP_LOG_NL);
	printf("------------------------------------------" APP_LOG_NL);
	for (uint8_t i = 0; i < 5; ++i) {
		uint16_t percent  = (float)count_ticks[i] / total * 1000.0f;
		uint32_t total_ms = (uint32_t)(count_ticks[i] * 1000 / freq);
		printf(
		    "| EM%d: %03ld.%03lds, %d.%01d%%" APP_LOG_NL,
		    i,
		    total_ms / 1000,
		    total_ms % 1000,
		    percent / 10,
		    percent % 10
		);
	}

#if defined(SL_POWER_MANAGER_DEBUG)
	sl_power_manager_debug_print_em_requirements();
#endif
}

void em_logger_init() {
	for (uint8_t i = 0; i < 5; ++i) {
		el.count_ticks[i] = 0;
	}
	el.last_transition = sl_sleeptimer_get_tick_count64();
	sl_power_manager_subscribe_em_transition_event(
	    &el.event_handle,
	    &el.event_info
	);
}

void _em_logger_on_pm_event(
    sl_power_manager_em_t from, sl_power_manager_em_t to
) {
	(void)to;
	if (from >= 5) {
		return;
	}
	uint64_t now = sl_sleeptimer_get_tick_count64();

	CORE_ATOMIC_SECTION({
		uint64_t ellapsed  = now - el.last_transition;
		el.last_transition = now;
		el.count_ticks[from] += ellapsed;
	});
}
