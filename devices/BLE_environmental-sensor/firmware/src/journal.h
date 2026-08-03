#pragma once

#include "sl_sleeptimer.h"
#include "sl_status.h"
#include "types.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif //__cplusplus

sl_status_t journal_init();

typedef uint32_t journal_index_t;

#define JOURNAL_INDEX_NPOS UINT32_MAX
#define JOURNAL_READ_CHUNK 16
#ifndef JOURNAL_SIZE
#define JOURNAL_SIZE                                                           \
	((journal_index_t)(SPIFLASH_SIZE / sizeof(journal_record_t)))
#endif // jOURNAL_SIZE

void journal_preempt_sleeping(bool preempt);

sl_status_t journal_add_record(const data_point_t *dp);

typedef void (*journal_lower_bound_callback_t)(
    sl_status_t               status,
    journal_index_t           index,
    sl_sleeptimer_timestamp_t timestamp,
    void                     *user_data
);

/// Returns the first index which timestamp is strictly smaller than time.
sl_status_t journal_find_last_before(
    sl_sleeptimer_timestamp_t      date,
    journal_lower_bound_callback_t callback,
    void                          *user_data
);

typedef void (*journal_read_callback_t)(
    sl_status_t status, const data_point_t *point, void *user_data
);

sl_status_t journal_read(
    journal_index_t         start,
    journal_index_t         end,
    journal_read_callback_t callback,
    void                   *user_data
);

#ifdef __cplusplus
}
#endif //__cplusplus
