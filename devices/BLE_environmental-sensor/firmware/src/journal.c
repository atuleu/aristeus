#include "journal.h"

#include <stddef.h>
#include <stdint.h>

#include "app_log.h"
#include "sl_core.h"
#include "sl_sleeptimer.h"
#include "sl_status.h"

#include "drivers/spiflash.h"
#include "journal_priv.h"
#include "journal_record.h"
#include "types.h"
#include "utils/status.h"

bool journal_input_queue_empty(journal_input_queue_t *q) {
	return q->head == q->tail;
}

bool journal_input_queue_full(journal_input_queue_t *q) {
	return ((q->head + 1) & INPUT_QUEUE_MASK) == q->tail;
}

uint8_t journal_input_queue_remaining(journal_input_queue_t *q) {
	if (q->tail > q->head) {
		return q->head - q->tail + INPUT_QUEUE_SIZE;
	}
	return q->head - q->tail;
}

bool journal_input_queue_add(journal_input_queue_t *q, const data_point_t *dp) {
	CORE_DECLARE_IRQ_STATE;
	CORE_ENTER_ATOMIC();
	if (journal_input_queue_full(q) == true) {
		CORE_EXIT_ATOMIC();
		return false;
	}
	q->data[q->head] = *dp;
	q->head          = (q->head + 1) & INPUT_QUEUE_MASK;
	CORE_EXIT_ATOMIC();
	return true;
}

bool journal_input_queue_pop(journal_input_queue_t *q, data_point_t *dp) {
	CORE_DECLARE_IRQ_STATE;
	CORE_ENTER_ATOMIC();
	if (journal_input_queue_empty(q) == true) {
		CORE_EXIT_ATOMIC();
		return false;
	}
	*dp     = q->data[q->tail];
	q->tail = (q->tail + 1) & INPUT_QUEUE_MASK;
	CORE_EXIT_ATOMIC();
	return true;
}

journal_t j = {
    .queue =
        {
            .head = 0,
            .tail = 0,
        },
    .operation        = journal_op_none,
    .preempt_sleeping = false
};

sl_status_t journal_add_record(const data_point_t *dp) {
	if (dp->date <= j.last_timestamp || dp->date == UINT32_MAX) {
		return SL_STATUS_INVALID_PARAMETER;
	}
	CORE_DECLARE_IRQ_STATE;
	CORE_ENTER_ATOMIC();
	uint8_t remaining = journal_input_queue_remaining(&j.queue);
	if ((j.next_index + remaining) >= JOURNAL_SIZE) {
		CORE_EXIT_ATOMIC();
		return SL_STATUS_FULL;
	}
	if (journal_input_queue_add(&j.queue, dp) == false) {
		CORE_EXIT_ATOMIC();
		return SL_STATUS_BUSY;
	}
	j.last_timestamp = dp->date;
	if (j.first_index == JOURNAL_INDEX_NPOS) {
		j.first_index     = j.next_index;
		j.first_timestamp = dp->date;
	}
	CORE_EXIT_ATOMIC();

	_journal_start_next_operation();
	return SL_STATUS_OK;
}

bool _journal_busy() {
	return j.operation != journal_op_none ||
	       j.operation_done != journal_op_none;
}

void _journal_start_next_operation() {
	CORE_DECLARE_IRQ_STATE;
	CORE_ENTER_ATOMIC();
	if (j.operation != journal_op_none) {
		CORE_EXIT_ATOMIC();
		return;
	}

	if (j.operation == journal_op_none && j.operation_done != journal_op_none) {
		CORE_EXIT_ATOMIC();
		// we will not start a new operation, we simply go to sleep early (if
		// not preempted)
		_journal_enter_deepsleep();
		return;
	}

	CORE_EXIT_ATOMIC();
	// we have no operation pending or completion pending
	if (_journal_start_next_write() == true) {
		return;
	}
	if (_journal_start_next_read() == true) {
		return;
	}
	if (_journal_start_next_find() == true) {
		return;
	}

	_journal_enter_deepsleep();
}

bool _journal_start_next_write() {
	data_point_t    dp;
	journal_index_t index;
	CORE_DECLARE_IRQ_STATE;
	CORE_ENTER_ATOMIC();

	if (journal_input_queue_pop(&j.queue, &dp) == false) {
		app_log_debug(
		    "[journal] attempting to write: nothing to write" APP_LOG_NL
		);
		CORE_EXIT_ATOMIC();
		return false;
	}

	if (j.next_index >= JOURNAL_SIZE) {
		CORE_EXIT_ATOMIC();
		app_log_error(
		    "[journal] attempting to write: no more size on device." APP_LOG_NL
		);
		return false;
	}

	j.operation = journal_op_write;
	index       = j.next_index;
	j.next_index += 1;
	CORE_EXIT_ATOMIC();

	app_log_info("[journal] writing at %ld ts=%ld." APP_LOG_NL, index, dp.date);
	journal_record_from_data_point(j.write_buffer.record, &dp);

	sl_status_t status = spiflash_write(
	    index * sizeof(journal_record_t),
	    j.write_buffer.bytes,
	    sizeof(journal_record_t),
	    &_journal_on_write,
	    NULL
	);

	if (status != SL_STATUS_OK) {
		CORE_ATOMIC_SECTION({
			j.operation = journal_op_none;
			j.next_index -= 1;
		});
		app_log_error(
		    "[journal] could not write: %s." APP_LOG_NL,
		    sl_status_get_string(status)
		);
		return false;
	}

	return true;
}

bool _journal_start_next_read() {
	CORE_DECLARE_IRQ_STATE;
	CORE_ENTER_ATOMIC();

	if (j.read_start >= j.read_end) {
		CORE_EXIT_ATOMIC();
		app_log_debug(
		    "[journal] attempting to read: nothing to read." APP_LOG_NL
		);
		return false;
	}

	j.operation = journal_op_read;
	CORE_EXIT_ATOMIC();

	_journal_read_next();
	return true;
}

bool _journal_start_next_find() {
	CORE_DECLARE_IRQ_STATE;
	CORE_ENTER_ATOMIC();

	if (j.find_callback == NULL) {
		CORE_EXIT_ATOMIC();
		app_log_debug(
		    "[journal] attempting to find: nothing to find." APP_LOG_NL
		);
		return false;
	}
	j.operation = journal_op_find;

	CORE_EXIT_ATOMIC();

	j.low    = j.first_index;
	j.low_ts = j.first_timestamp;
	j.high   = j.next_index - 1;
	if (j.last_timestamp < j.target) {
		_journal_complete_find(
		    SL_STATUS_OK,
		    j.next_index - 1,
		    j.last_timestamp
		);
		return true;
	}
	j.under_read           = (j.low + j.high) / 2;
	j.bad_crc_towards_high = true;
	_journal_find_step();

	return true;
}

void _journal_on_write(sl_status_t status, void *user_data) {
	(void)user_data;
	CORE_ATOMIC_SECTION({ j.operation = journal_op_none; });
	if (status != SL_STATUS_OK) {
		app_log_error(
		    "[journal] could not write record: %s." APP_LOG_NL,
		    sl_status_get_string(status)
		);
	}
	_journal_start_next_operation();
}

sl_status_t journal_read(
    journal_index_t         start,
    journal_index_t         end,
    journal_read_callback_t callback,
    void                   *user_data
) {
	if (start >= JOURNAL_SIZE || end > JOURNAL_SIZE) {
		return SL_STATUS_INVALID_RANGE;
	}
	if (start >= end) {
		return SL_STATUS_INVALID_PARAMETER;
	}

	CORE_DECLARE_IRQ_STATE;
	CORE_ENTER_ATOMIC();
	if (j.read_callback != NULL) {
		CORE_EXIT_ATOMIC();
		return SL_STATUS_BUSY;
	}
	j.read_callback  = callback;
	j.read_user_data = user_data;
	j.read_start     = start;
	j.read_end       = end;
	CORE_EXIT_ATOMIC();

	_journal_start_next_operation();

	return SL_STATUS_OK;
}

void _journal_read_next() {
	journal_index_t end = j.read_start + JOURNAL_READ_CHUNK;
	if (end > j.read_end) {
		end = j.read_end;
	}
	sl_status_t status = spiflash_read(
	    j.read_start * sizeof(journal_record_t),
	    j.buffer.bytes,
	    sizeof(journal_record_t) * (end - j.read_start),
	    &_journal_mark_current_operation_done,
	    NULL
	);

	if (status != SL_STATUS_OK) {
		_journal_mark_current_operation_done(
		    SL_STATUS_FLASH_VERIFY_FAILED,
		    NULL
		);
	}
}

void _journal_mark_current_operation_done(sl_status_t status, void *user_data) {
	(void)user_data;
	CORE_ATOMIC_SECTION({
		j.operation_done        = j.operation;
		j.operation_done_status = status;
		j.operation             = journal_op_none;
	});

	_journal_enter_deepsleep();
}

void _journal_on_chunk_read(sl_status_t status) {

	if (status != SL_STATUS_OK) {
		_journal_complete_read_from_main(status);
		return;
	}

	journal_index_t count = j.read_end - j.read_start;

	if (count > JOURNAL_READ_CHUNK) {
		count = JOURNAL_READ_CHUNK;
	}

	for (uint8_t i = 0; i < count; ++i) {
		const journal_record_t *record = &j.buffer.records[i];
		if (journal_record_check_crc(record) == true) {
			_journal_read_send_data_point(record);
		} else {
			app_log_warning(
			    "[journal] Bad CRC at address %ld." APP_LOG_NL,
			    j.read_start + i
			);
		}
	}

	j.read_start += count;

	if (j.read_start >= j.read_end) {
		_journal_complete_read_from_main(SL_STATUS_OK);
	}
}

void _journal_read_send_data_point(const journal_record_t *record) {
	data_point_t dp;
	journal_record_to_data_point(record, &dp);
	journal_read_callback_t callback;
	void                   *user_data;
	CORE_ATOMIC_SECTION({
		callback  = j.read_callback;
		user_data = j.read_user_data;
	});
	if (callback == NULL) {
		return;
	}
	callback(SL_STATUS_OK, &dp, user_data);
}

/// Returns the first index which timestamp is strictly smaller than time.
sl_status_t journal_find_last_before(
    sl_sleeptimer_timestamp_t date,
    journal_find_callback_t   callback,
    void                     *user_data
) {
	if (callback == NULL) {
		return SL_STATUS_NULL_POINTER;
	}
	CORE_DECLARE_IRQ_STATE;
	CORE_ENTER_ATOMIC();
	if (j.first_index == JOURNAL_INDEX_NPOS) {
		CORE_EXIT_ATOMIC();
		return SL_STATUS_EMPTY;
	}

	if (date <= j.first_timestamp) {
		CORE_EXIT_ATOMIC();
		return SL_STATUS_INVALID_RANGE;
	}

	if (j.next_index == JOURNAL_INDEX_NPOS) {
		CORE_EXIT_ATOMIC();
		return SL_STATUS_INITIALIZATION;
	}

	if (j.find_callback != NULL) {
		CORE_EXIT_ATOMIC();
		return SL_STATUS_BUSY;
	}

	j.target         = date;
	j.find_callback  = callback;
	j.find_user_data = user_data;
	j.low            = JOURNAL_INDEX_NPOS;
	j.high           = JOURNAL_INDEX_NPOS;
	CORE_EXIT_ATOMIC();

	_journal_start_next_operation();

	return SL_STATUS_OK;
}

void _journal_find_step() {
	sl_status_t status = spiflash_read(
	    j.under_read * sizeof(journal_record_t),
	    j.find_buffer.bytes,
	    sizeof(journal_record_header_t),
	    &_journal_on_find_step,
	    NULL
	);
	if (status != SL_STATUS_OK) {
		_journal_complete_find(status, JOURNAL_INDEX_NPOS, UINT32_MAX);
	}
}

sl_status_t _journal_read_timestamp(sl_sleeptimer_timestamp_t *ts) {
	if (j.find_buffer.header.timestamp == UINT32_MAX &&
	    j.find_buffer.header.crc == 0xFF) {
		*ts = UINT32_MAX;
		return SL_STATUS_OK;
	}
	if (journal_record_header_check_crc(&j.find_buffer.header) == false) {
		return SL_STATUS_FLASH_VERIFY_FAILED;
	}
	*ts = j.find_buffer.header.timestamp;
	return SL_STATUS_OK;
}

void _journal_on_find_step(sl_status_t status, void *user_data) {
	(void)user_data;
	if (status != SL_STATUS_OK) {
		_journal_complete_find(
		    SL_STATUS_FLASH_VERIFY_FAILED,
		    JOURNAL_INDEX_NPOS,
		    UINT32_MAX
		);
		return;
	}
	sl_sleeptimer_timestamp_t timestamp;
	status = _journal_read_timestamp(&timestamp);

	// mitigate wrong CRC
	if (status != SL_STATUS_OK) {
		if (j.bad_crc_towards_high == true) {
			if (j.under_read < (j.high - 1)) {
				j.under_read += 1;
				_journal_find_step();
				return;
			} else {
				j.bad_crc_towards_high = false;
				j.under_read           = (j.low + j.high) / 2 - 1;
				if (j.under_read > j.low) {
					_journal_find_step();
					return;
				}
				_journal_complete_find(SL_STATUS_OK, j.low, j.low_ts);
				return;
			}
		} else if (j.under_read > (j.low + 1)) {
			j.bad_crc_towards_high = false;
			j.under_read -= 1;
			_journal_find_step();
			return;
		} else {
			_journal_complete_find(SL_STATUS_OK, j.low, j.low_ts);
			return;
		}
	}
	j.bad_crc_towards_high = true;

	if (timestamp >= j.target) {
		j.high = j.under_read;
		if (j.high == (j.low + 1)) {
			_journal_complete_find(SL_STATUS_OK, j.low, j.low_ts);
			return;
		}
	} else {
		j.low    = j.under_read;
		j.low_ts = timestamp;
		if (j.high == (j.low + 1)) {
			_journal_complete_find(SL_STATUS_OK, j.low, j.low_ts);
			return;
		}
	}
	j.under_read = (j.high + j.low) / 2;
	_journal_find_step();
}

void _journal_complete_find(
    sl_status_t status, journal_index_t index, sl_sleeptimer_timestamp_t ts
) {
	CORE_ATOMIC_SECTION({
		j.low    = index;
		j.low_ts = ts;
		_journal_mark_current_operation_done(status, NULL);
	});
}

sl_status_t journal_init() {

	j.queue.head            = 0;
	j.queue.tail            = 0;
	j.preempt_sleeping      = false;
	j.operation             = journal_op_init;
	j.operation_done        = journal_op_none;
	j.operation_done_status = SL_STATUS_OK;

	j.next_index      = JOURNAL_INDEX_NPOS;
	j.first_index     = JOURNAL_INDEX_NPOS;
	j.last_timestamp  = UINT32_MAX;
	j.first_timestamp = UINT32_MAX;

	j.read_start    = 0;
	j.read_end      = 0;
	j.read_callback = NULL;

	// this will prevent any write before initialization;
	j.find_callback = NULL;
	j.under_read    = 0;

	sl_status_t status = spiflash_read(
	    sizeof(journal_record_t) * j.under_read,
	    j.find_buffer.bytes,
	    sizeof(journal_record_header_t),
	    &_journal_on_read_first_idx,
	    NULL
	);
	if (status != SL_STATUS_OK) {
		CORE_ATOMIC_SECTION({ j.operation = journal_op_none; });
		app_log_error(
		    "[journal] could not look for first index %ld: %s." APP_LOG_NL,
		    j.under_read,
		    sl_status_get_string(status)
		);
	}

	return status;
}

void _journal_on_read_first_idx(sl_status_t status, void *user_data) {
	(void)user_data;
	if (status != SL_STATUS_OK) {
		app_log_error(
		    "[journal] could not find first index %ld: %s." APP_LOG_NL,
		    j.under_read,
		    sl_status_get_string(status)
		);
		return;
	}
	sl_sleeptimer_timestamp_t ts;
	status = _journal_read_timestamp(&ts);
	if (status == SL_STATUS_OK) {
		if (ts == UINT32_MAX) {
			app_log_info(
			    "[journal] found erased memory at index %ld." APP_LOG_NL,
			    j.under_read
			);
			// uninitialized memory
			// disable search as we are empty.
			j.first_index     = JOURNAL_INDEX_NPOS;
			j.first_timestamp = UINT32_MAX;
			// enable write at index under_read, (previous could have bad CRC
			j.next_index      = j.under_read;
			j.last_timestamp  = 0;
			CORE_ATOMIC_SECTION({ j.operation = journal_op_none; });
			_journal_start_next_operation();
			return;
		}
		j.first_index     = j.under_read;
		j.first_timestamp = ts;
		app_log_info(
		    "[journal] found first index at %ld with ts=%ld." APP_LOG_NL,
		    j.first_index,
		    j.first_timestamp
		);

		j.under_read = JOURNAL_SIZE - 1;
		status       = spiflash_read(
            j.under_read * sizeof(journal_record_t),
            j.find_buffer.bytes,
            sizeof(journal_record_header_t),
            &_journal_on_read_last_idx,
            NULL
        );

		if (status != SL_STATUS_OK) {
			CORE_ATOMIC_SECTION({ j.operation = journal_op_none; });
			app_log_error(
			    "[journal] could not look for last index %ld: "
			    "%s." APP_LOG_NL,
			    j.under_read,
			    sl_status_get_string(status)
			);
			_journal_start_next_operation();
		}
		return;
	}

	// bad CRC path
	j.under_read += 1;
	if (j.under_read >= JOURNAL_SIZE) {
		app_log_error("[journal] memory is initialized with bad memory");
		CORE_ATOMIC_SECTION({ j.operation = journal_op_none; });
		_journal_start_next_operation();
		return;
	}

	status = spiflash_read(
	    sizeof(journal_record_t) * j.under_read,
	    j.find_buffer.bytes,
	    sizeof(journal_record_header_t),
	    _journal_on_read_first_idx,
	    NULL
	);

	if (status != SL_STATUS_OK) {
		CORE_ATOMIC_SECTION({ j.operation = journal_op_none; });
		app_log_error(
		    "[journal] could not lookup first index %ld: %s." APP_LOG_NL,
		    j.under_read,
		    sl_status_get_string(status)
		);
		_journal_start_next_operation();
	}

	return;
}

void _journal_on_read_last_idx(sl_status_t status, void *user_data) {
	(void)user_data;
	if (status != SL_STATUS_OK) {
		app_log_error(
		    "[journal] could not read last index %ld: %s." APP_LOG_NL,
		    j.under_read,
		    sl_status_get_string(status)
		);
		CORE_ATOMIC_SECTION({ j.operation = journal_op_none; });
		_journal_start_next_operation();
		return;
	}
	sl_sleeptimer_timestamp_t ts;
	status = _journal_read_timestamp(&ts);
	if (status == SL_STATUS_OK) {
		if (ts != UINT32_MAX) {
			j.last_timestamp = ts;
			j.next_index     = JOURNAL_SIZE;
			CORE_ATOMIC_SECTION({ j.operation = journal_op_none; });
			_journal_start_next_operation();
			return;
		}
		j.operation      = journal_op_find;
		j.high           = j.under_read;
		j.target         = UINT32_MAX;
		j.under_read     = (j.high + j.low) / 2;
		j.find_callback  = _journal_on_find_next_idx;
		j.find_user_data = NULL;
		_journal_find_step();
		return;
	}

	// bad crc path
	j.under_read -= 1;
	status = spiflash_read(
	    j.under_read * sizeof(journal_record_t),
	    j.find_buffer.bytes,
	    sizeof(journal_record_header_t),
	    &_journal_on_read_last_idx,
	    NULL
	);
	if (status != SL_STATUS_OK) {
		CORE_ATOMIC_SECTION({ j.operation = journal_op_none; });
		app_log_error(
		    "could not look up for last index %ld: %s.",
		    j.under_read,
		    sl_status_get_string(status)
		);
	}
	return;
}

void _journal_on_find_next_idx(
    sl_status_t               status,
    journal_index_t           index,
    sl_sleeptimer_timestamp_t timestamp,
    void                     *user_data
) {
	(void)status;
	(void)user_data;
	if (status != SL_STATUS_OK) {
		app_log_error(
		    "[journal] could not find starting index from flash: "
		    "%s." APP_LOG_NL,
		    sl_status_get_string(status)
		);
		j.next_index     = JOURNAL_INDEX_NPOS;
		j.last_timestamp = UINT32_MAX;
		return;
	}
	j.last_timestamp = timestamp;
	j.next_index     = index + 1;
	app_log_info(
	    "[journal] found next index at %ld for times > %ld." APP_LOG_NL,
	    j.next_index,
	    j.last_timestamp
	);
}

void _journal_enter_deepsleep() {
	CORE_DECLARE_IRQ_STATE;
	CORE_ENTER_ATOMIC();
	if (j.operation != journal_op_none || j.preempt_sleeping == true) {
		CORE_EXIT_ATOMIC();
		return;
	}
	j.operation = journal_op_sleep;
	CORE_EXIT_ATOMIC();

	sl_status_t status = spiflash_enter_deepsleep(&_journal_on_sleep, NULL);
	if (status != SL_STATUS_OK) {
		CORE_ATOMIC_SECTION({ j.operation = journal_op_none; });
		if (status != SL_STATUS_ALREADY_INITIALIZED) {
			// we may have spurious sleeping attempt, so no worries if we are
			// already sleeping.
			app_log_error(
			    "[journal] could not enter deepsleep: %s." APP_LOG_NL,
			    sl_status_get_string(status)
			);
		}
		return;
	}
}

void _journal_on_sleep(sl_status_t status, void *user_data) {
	(void)user_data;
	if (status != SL_STATUS_OK) {
		app_log_warning(
		    "[journal] sleep failed: %s." APP_LOG_NL,
		    sl_status_get_string(status)
		);
	}
	CORE_ATOMIC_SECTION({ j.operation = journal_op_none; });
	app_log_info("[journal] sleeping." APP_LOG_NL);
}

void journal_preempt_sleeping(bool preempt) {
	CORE_DECLARE_IRQ_STATE;
	CORE_ENTER_ATOMIC();
	if (preempt == true) {
		j.preempt_sleeping = true;
		CORE_EXIT_ATOMIC();
		return;
	}
	j.preempt_sleeping = false;
	_journal_enter_deepsleep();
	CORE_EXIT_ATOMIC();
}

sl_status_t journal_erase() {
	CORE_DECLARE_IRQ_STATE;
	CORE_ENTER_ATOMIC();
	if (_journal_busy() == true) {
		CORE_EXIT_ATOMIC();
		return SL_STATUS_BUSY;
	}
	j.operation = journal_op_erase;
	CORE_EXIT_ATOMIC();

	sl_status_t status =
	    spiflash_erase(0, SPIFLASH_SIZE, &_journal_on_erase, NULL);

	if (status == SL_STATUS_OK) {
		CORE_ATOMIC_SECTION(j.operation = journal_op_none;);
	}

	return status;
}

void _journal_on_erase(sl_status_t status, void *user_data) {
	(void)user_data;
	CORE_DECLARE_IRQ_STATE;
	CORE_ENTER_ATOMIC();
	if (status == SL_STATUS_OK) {
		j.first_index     = JOURNAL_INDEX_NPOS;
		j.first_timestamp = UINT32_MAX;
		j.last_timestamp  = 0;
		j.next_index      = 0;
	}
	j.operation = journal_op_none;
	CORE_EXIT_ATOMIC();
	_journal_start_next_operation();
}

void journal_process_action() {
	journal_operation_t finished_operation;
	sl_status_t         status;
	CORE_ATOMIC_SECTION({
		finished_operation = j.operation_done;
		status             = j.operation_done_status;
		j.operation_done   = journal_op_none;
	});
	switch (finished_operation) {
	case journal_op_none:
		return;
	case journal_op_read:
		_journal_on_chunk_read(status);
		break;
	case journal_op_find:
		_journal_on_find_done(status);
		break;
	default:
		app_log_error(
		    "[journal] spurious operation done %d with status %s." APP_LOG_NL,
		    finished_operation,
		    sl_status_get_string(status)
		);
	}
	_journal_start_next_operation();
}

void _journal_complete_read_from_main(sl_status_t status) {
	journal_read_callback_t callback;
	void                   *user_data;
	CORE_ATOMIC_SECTION({
		callback         = j.read_callback;
		user_data        = j.read_user_data;
		j.read_callback  = NULL;
		j.read_user_data = NULL;
	});

	if (callback == NULL) {
		return;
	}
	callback(status, NULL, user_data);
}

void _journal_on_find_done(sl_status_t status) {
	journal_find_callback_t callback;
	void                   *user_data;
	CORE_ATOMIC_SECTION({
		callback         = j.find_callback;
		user_data        = j.find_user_data;
		j.find_callback  = NULL;
		j.find_user_data = NULL;
	});

	if (callback == NULL) {
		return;
	}
	callback(status, j.low, j.low_ts, user_data);
}

bool journal_is_ok_to_sleep() {
	// we can sleep if there is no completion pending, that requires the
	// main loop
	return j.operation_done == journal_op_none;
}
