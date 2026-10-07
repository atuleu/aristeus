#pragma once

#include "journal.h"
#include "journal_record.h"
#include "sl_enum.h"
#include "sl_sleeptimer.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif //__cplusplus
#define INPUT_QUEUE_SIZE 32
#define INPUT_QUEUE_MASK (INPUT_QUEUE_SIZE - 1)
static_assert(
    (INPUT_QUEUE_SIZE & INPUT_QUEUE_MASK) == 0,
    "Input queue size must be a power of two"
);
static_assert(
    INPUT_QUEUE_SIZE <= 256, "Input queue must be smaller than 256 items"
);

typedef struct journal_input_queue {
	data_point_t data[INPUT_QUEUE_SIZE];
	uint8_t      head, tail;
} journal_input_queue_t;

bool journal_input_queue_empty(journal_input_queue_t *q);
bool journal_input_queue_full(journal_input_queue_t *q);
bool journal_input_queue_add(journal_input_queue_t *q, const data_point_t *dp);
bool journal_input_queue_pop(journal_input_queue_t *q, data_point_t *dp);
SL_ENUM(journal_operation_t){
    journal_op_none = 0x00,
    journal_op_write,
    journal_op_find,
    journal_op_read,
    journal_op_init,
    journal_op_sleep,
    journal_op_erase,
};

SL_ENUM(journal_read_state_t){
    journal_read_state_idle = 0,
    journal_read_state_need_chunk,
    journal_read_state_reading_chunk,
    journal_read_state_paused,
    journal_read_state_resume,
    journal_read_state_abord,
};

typedef struct journal {
	journal_input_queue_t        queue;
	bool                         preempt_sleeping;
	volatile journal_operation_t operation, operation_done;
	volatile sl_status_t         operation_done_status;

	union {
		journal_record_t records[JOURNAL_READ_CHUNK];
		uint8_t          bytes[JOURNAL_READ_CHUNK * sizeof(journal_record_t)];
	} buffer;

	union {
		journal_record_t record[1];
		uint8_t          bytes[sizeof(journal_record_t)];
	} write_buffer;

	journal_index_t           next_index, first_index;
	sl_sleeptimer_timestamp_t first_timestamp, last_timestamp,
	    last_queued_timestamp;

	volatile journal_read_state_t      read_state;
	volatile journal_index_t           read_start, read_end;
	uint8_t                            read_chunk_idx, read_chunk_size;
	journal_read_callback_t            read_callback;
	void                              *read_user_data;
	journal_find_callback_t            find_callback;
	void                              *find_user_data;
	sl_sleeptimer_timestamp_t          target;
	volatile sl_sleeptimer_timestamp_t low_ts;
	volatile journal_index_t           low, high, under_read;
	volatile bool                      bad_crc_towards_high;

	union {
		journal_record_header_t header;
		uint8_t                 bytes[5];
	} find_buffer;
} journal_t;

extern journal_t j;

bool _journal_busy();

void _journal_start_next_operation();
bool _journal_start_next_write();
bool _journal_start_next_read();
bool _journal_start_next_find();
void _journal_enter_deepsleep();

void _journal_mark_current_operation_done(sl_status_t status, void *user_data);

void _journal_on_write(sl_status_t status, void *user_data);
void _journal_on_sleep(sl_status_t status, void *user_data);

void _journal_read_current_chunk(sl_status_t status);

void _journal_on_find_step(sl_status_t status, void *user_data);
void _journal_on_find_done(sl_status_t status);
void _journal_on_erase(sl_status_t status, void *user_data);

void _journal_find_step();

void _journal_complete_find(
    sl_status_t status, journal_index_t index, sl_sleeptimer_timestamp_t ts
);
void _journal_on_read_done(sl_status_t status);
void _journal_on_read_first_idx(sl_status_t status, void *user_data);
void _journal_on_read_last_idx(sl_status_t status, void *user_data);
void _journal_on_find_last_written_idx(
    sl_status_t               status,
    journal_index_t           index,
    sl_sleeptimer_timestamp_t ts,
    void                     *user_data
);

#ifdef __cplusplus
}
#endif //__cplusplus
