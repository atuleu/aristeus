#include "journal.h"
#include "app_log.h"
#include "drivers/spiflash.h"
#include "sl_core.h"
#include "sl_enum.h"
#include "sl_sleeptimer.h"
#include "sl_status.h"
#include "spidrv.h"
#include "types.h"
#include "utils/crc8.h"
#include <stddef.h>
#include <stdint.h>

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

bool journal_input_queue_empty(journal_input_queue_t *q) {
	return q->head == q->tail;
}

bool journal_input_queue_full(journal_input_queue_t *q) {
	return ((q->head + 1) & INPUT_QUEUE_MASK) == q->tail;
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

SL_ENUM(journal_operation_t){
    journal_op_none = 0x00,
    journal_op_write,
    journal_op_find,
    journal_op_read,
};

typedef struct __attribute__((packed)) journal_record_header {
	sl_sleeptimer_timestamp_t timestamp;
	uint8_t                   crc;

} journal_record_header_t;

static_assert(sizeof(journal_record_header_t) == 5, "incorrect header size");

typedef struct __attribute__((packed)) journal_record_data {
	temperature_t       temperature;
	humidity_t          humidity;
	pressure_t          pressure;
	co2_concentration_t co2;
	uint8_t             crc;
} journal_record_data_t;

static_assert(
    sizeof(journal_record_data_t) == 11, "incorrect record data size"
);

typedef struct __attribute__((packed)) journal_record {
	journal_record_header_t header;
	journal_record_data_t   data;
} journal_record_t;

static_assert(sizeof(journal_record_t) == 16, "non aligned record size");

uint8_t _journal_record_compute_crc(const uint8_t *buffer, uint8_t size) {
	uint8_t crc = 0xff;
	for (uint8_t i = 0; i < size; ++i) {
		crc = CRC8_AppendByte(crc, 0x31, buffer[i]);
	}
	return crc;
}

void _journal_record_from_data_point(
    journal_record_t *r, const data_point_t *dp
) {
	r->header.timestamp = dp->date;
	r->header.crc       = _journal_record_compute_crc(
        (uint8_t *)&r->header.timestamp,
        sizeof(journal_record_header_t) - sizeof(uint8_t)
    );
	r->data.temperature = dp->temperature;
	r->data.humidity    = dp->humidity;
	r->data.pressure    = dp->pressure;
	r->data.co2         = dp->c02;
	r->data.crc         = _journal_record_compute_crc(
        (uint8_t *)&r->data,
        sizeof(journal_record_data_t) - sizeof(uint8_t)
    );
}

void _journal_record_to_data_point(
    const journal_record_t *r, data_point_t *dp
) {
	dp->date        = r->header.timestamp;
	dp->temperature = r->data.temperature;
	dp->humidity    = r->data.humidity;
	dp->pressure    = r->data.pressure;
	dp->c02         = r->data.co2;
}

bool _journal_record_header_check_crc(const journal_record_header_t *r) {
	return _journal_record_compute_crc(
	           (const uint8_t *)r,
	           sizeof(journal_record_header_t)
	       ) == 0;
}

bool _journal_record_check_crc(const journal_record_t *r) {
	return _journal_record_header_check_crc(&r->header) &&
	       _journal_record_compute_crc(
	           (const uint8_t *)&r->data,
	           sizeof(journal_record_data_t)
	       ) == 0;
}

#define JOURNAL_READ_CHUNK 16
#define JOURNAL_RECORD_SIZE                                                    \
	((journal_index_t)(SPIFLASH_SIZE / sizeof(journal_record_t)))

typedef struct journal {
	journal_input_queue_t queue;
	journal_operation_t   operation;

	union {
		journal_record_t records[JOURNAL_READ_CHUNK];
		uint8_t          bytes[JOURNAL_READ_CHUNK * sizeof(journal_record_t)];
	} buffer;

	journal_index_t           next_index;
	sl_sleeptimer_timestamp_t last_timestamp;

	void *user_data;

	journal_index_t         read_start;
	journal_index_t         read_end;
	journal_read_callback_t read_callback;

	journal_lower_bound_callback_t find_callback;
	sl_sleeptimer_timestamp_t      low_ts, target;
	journal_index_t                low, high, under_read;
	bool                           bad_crc_towards_high;

	union {
		journal_record_header_t header;
		uint8_t                 bytes[5];
	} find_buffer;
} journal_t;

static journal_t j = {
    .queue =
        {
            .head = 0,
            .tail = 0,
        },
    .operation = journal_op_none,
};

void _journal_may_start_write();

void _journal_on_write(sl_status_t status, void *user_data);

void _journal_read_send_data_point(const journal_record_t *record);
void        _journal_complete_read(sl_status_t status);
sl_status_t _journal_read_next(bool call_callback);
void _journal_on_read(sl_status_t status, void *user_data);

void        _journal_on_find(sl_status_t status, void *user_data);
sl_status_t _journal_find_step(bool call_callback);
void        _journal_complete_find(
           sl_status_t status, journal_index_t index, sl_sleeptimer_timestamp_t ts
       );
void        _journal_on_find_next_idx(
           sl_status_t               status,
           journal_index_t           index,
           sl_sleeptimer_timestamp_t ts,
           void                     *user_data
       );

sl_status_t journal_add_record(const data_point_t *dp) {
	if (dp->date <= j.last_timestamp || dp->date == UINT32_MAX) {
		return SL_STATUS_INVALID_PARAMETER;
	}
	CORE_DECLARE_IRQ_STATE;
	CORE_ENTER_ATOMIC();
	if (j.next_index >= JOURNAL_RECORD_SIZE ||
	    journal_input_queue_add(&j.queue, dp) == false) {
		CORE_EXIT_ATOMIC();
		return SL_STATUS_FULL;
	}
	j.last_timestamp = dp->date;
	CORE_EXIT_ATOMIC();

	_journal_may_start_write();
	return SL_STATUS_OK;
}

void _journal_may_start_write() {
	data_point_t dp;
	CORE_DECLARE_IRQ_STATE;
	CORE_ENTER_ATOMIC();
	if (j.operation != journal_op_none ||
	    journal_input_queue_pop(&j.queue, &dp) == false) {
		CORE_EXIT_ATOMIC();
		return;
	}

	if (j.next_index >= JOURNAL_RECORD_SIZE) {
		CORE_EXIT_ATOMIC();
		app_log_error("[journal] no more size on device.");
		return;
	}

	j.operation = journal_op_write;
	CORE_EXIT_ATOMIC();

	_journal_record_from_data_point(j.buffer.records, &dp);

	sl_status_t status = spiflash_write(
	    j.next_index * sizeof(journal_record_t),
	    j.buffer.bytes,
	    sizeof(journal_record_t),
	    &_journal_on_write,
	    NULL
	);

	if (status != SL_STATUS_OK) {
		CORE_ATOMIC_SECTION({ j.operation = journal_op_none; });
		return;
	}

	j.next_index += 1;
}

void _journal_on_write(sl_status_t status, void *user_data) {
	(void)user_data;
	if (status != SL_STATUS_OK) {
		app_log_error(
		    "[journal] could not write record: 0x%04lX." APP_LOG_NL,
		    status
		);
	}
	_journal_may_start_write();
}

sl_status_t journal_read(
    journal_index_t         start,
    journal_index_t         end,
    journal_read_callback_t callback,
    void                   *user_data
) {
	if (start >= JOURNAL_RECORD_SIZE || end > JOURNAL_RECORD_SIZE) {
		return SL_STATUS_INVALID_RANGE;
	}
	if (start >= end) {
		return SL_STATUS_INVALID_PARAMETER;
	}

	CORE_DECLARE_IRQ_STATE;
	CORE_ENTER_ATOMIC();
	if (j.operation != journal_op_none) {
		CORE_EXIT_ATOMIC();
		return SL_STATUS_BUSY;
	}
	j.operation     = journal_op_read;
	j.read_callback = callback;
	j.user_data     = user_data;
	CORE_EXIT_ATOMIC();
	j.read_start = start;
	j.read_end   = end;

	sl_status_t status = _journal_read_next(false);
	if (status != SL_STATUS_OK) {
		CORE_ATOMIC_SECTION({
			j.operation     = journal_op_none;
			j.read_callback = NULL;
			j.user_data     = NULL;
		});
	}
	return status;
}

sl_status_t _journal_read_next(bool call_callback) {
	journal_index_t end = j.read_start + JOURNAL_READ_CHUNK;
	if (end > j.read_end) {
		end = j.read_end;
	}
	sl_status_t status = spiflash_read(
	    j.read_start * sizeof(journal_record_t),
	    j.buffer.bytes,
	    sizeof(journal_record_t) * (end - j.read_start),
	    &_journal_on_read,
	    NULL
	);
	if (status != SL_STATUS_OK && call_callback == true) {
		_journal_complete_read(SL_STATUS_FLASH_VERIFY_FAILED);
	}
	return status;
}

void _journal_on_read(sl_status_t status, void *user_data) {
	(void)user_data;
	if (status != SL_STATUS_OK) {
		_journal_complete_read(SL_STATUS_FLASH_VERIFY_FAILED);
		return;
	}
	journal_index_t count = j.read_end - j.read_start;
	if (count > JOURNAL_READ_CHUNK) {
		count = JOURNAL_READ_CHUNK;
	}
	for (uint8_t i = 0; i < count; ++i) {
		const journal_record_t *record = &j.buffer.records[i];
		if (_journal_record_check_crc(record) == true) {
			_journal_read_send_data_point(record);
		}
	}
	j.read_start += count;
	if (j.read_start < j.read_end) {
		_journal_read_next(true);
	}
}

void _journal_read_send_data_point(const journal_record_t *record) {
	data_point_t dp;
	_journal_record_to_data_point(record, &dp);
	journal_read_callback_t callback;
	void                   *user_data;
	CORE_ATOMIC_SECTION({
		callback  = j.read_callback;
		user_data = j.user_data;
	});
	if (callback == NULL) {
		return;
	}
	callback(SL_STATUS_OK, &dp, user_data);
}

void _journal_complete_read(sl_status_t status) {
	journal_read_callback_t callback;
	void                   *user_data;
	CORE_ATOMIC_SECTION({
		callback        = j.read_callback;
		user_data       = j.user_data;
		j.operation     = journal_op_none;
		j.read_callback = NULL;
		j.user_data     = NULL;
	});
	if (callback == NULL) {
		return;
	}
	callback(status, NULL, user_data);

	_journal_may_start_write();
}

/// Returns the first index which timestamp is strictly smaller than time.
sl_status_t journal_find_last_before(
    sl_sleeptimer_timestamp_t      date,
    journal_lower_bound_callback_t callback,
    void                          *user_data
) {

	CORE_DECLARE_IRQ_STATE;
	CORE_ENTER_ATOMIC();
	if (j.operation != journal_op_none) {
		CORE_EXIT_ATOMIC();
		return SL_STATUS_BUSY;
	}
	j.operation = journal_op_find;

	j.find_callback = callback;
	j.user_data     = user_data;
	CORE_EXIT_ATOMIC();

	j.target               = date;
	j.low                  = JOURNAL_INDEX_NPOS;
	j.high                 = JOURNAL_INDEX_NPOS;
	j.under_read           = 0;
	j.bad_crc_towards_high = true;
	sl_status_t status     = _journal_find_step(false);
	if (status != SL_STATUS_OK) {
		CORE_ATOMIC_SECTION({ j.operation = journal_op_none; });
	}
	return status;
}

sl_status_t _journal_find_step(bool call_callback) {
	sl_status_t status = spiflash_read(
	    j.under_read * sizeof(journal_record_t),
	    j.find_buffer.bytes,
	    sizeof(journal_record_header_t),
	    &_journal_on_find,
	    NULL
	);
	if (status != SL_STATUS_OK && call_callback == true) {
		_journal_complete_find(status, JOURNAL_INDEX_NPOS, UINT32_MAX);
	}
	return status;
}

sl_status_t _journal_read_timestamp(sl_sleeptimer_timestamp_t *ts) {
	if (j.find_buffer.header.timestamp == UINT32_MAX &&
	    j.find_buffer.header.crc == 0xFF) {
		*ts = UINT32_MAX;
		return SL_STATUS_OK;
	}
	if (_journal_record_header_check_crc(&j.find_buffer.header) == false) {
		return SL_STATUS_FLASH_VERIFY_FAILED;
	}
	*ts = j.find_buffer.header.timestamp;
	return SL_STATUS_OK;
}

void _journal_on_find(sl_status_t status, void *user_data) {
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
		if (j.low == JOURNAL_INDEX_NPOS) {
			if (j.under_read == (JOURNAL_RECORD_SIZE - 1)) {
				_journal_complete_find(
				    SL_STATUS_FAIL,
				    JOURNAL_INDEX_NPOS,
				    UINT32_MAX
				);
				return;
			}
			j.under_read += 1;
			_journal_find_step(true);
			return;
		} else if (j.high == JOURNAL_INDEX_NPOS) {
			if (j.under_read == 0) {
				_journal_complete_find(
				    SL_STATUS_FAIL,
				    JOURNAL_INDEX_NPOS,
				    UINT32_MAX
				);
				return;
			}
			j.under_read -= 1;
			_journal_find_step(true);
			return;
		} else if (j.bad_crc_towards_high == true) {
			if (j.under_read < (j.high - 1)) {
				j.under_read += 1;
				_journal_find_step(true);

				return;
			} else {
				j.bad_crc_towards_high = false;
				j.under_read           = (j.low + j.high) / 2 - 1;
				if (j.under_read > j.low) {
					_journal_find_step(true);
					return;
				}
				_journal_complete_find(SL_STATUS_OK, j.low, j.low_ts);
				return;
			}
		} else if (j.under_read > (j.low + 1)) {
			j.bad_crc_towards_high = false;
			j.under_read -= 1;
			_journal_find_step(true);
			return;
		} else {
			_journal_complete_find(SL_STATUS_OK, j.low, j.low_ts);
			return;
		}
	}
	j.bad_crc_towards_high = true;

	if (j.low == JOURNAL_INDEX_NPOS) {
		if (timestamp >= j.target) {
			if (timestamp == UINT32_MAX) {
				// there will be no smaller than this, but still we found the
				// last
				_journal_complete_find(SL_STATUS_EMPTY, j.under_read - 1, 0);
			} else {
				_journal_complete_find(
				    SL_STATUS_FAIL,
				    JOURNAL_INDEX_NPOS,
				    UINT32_MAX
				);
			}
			return;
		}
		j.low        = j.under_read;
		j.low_ts     = timestamp;
		j.under_read = JOURNAL_RECORD_SIZE - 1;
		_journal_find_step(true);
		return;
	} else if (j.high == JOURNAL_INDEX_NPOS) {
		if (timestamp < j.target) {
			_journal_complete_find(SL_STATUS_OK, j.under_read, timestamp);
			return;
		}
		j.high       = j.under_read;
		j.under_read = (j.high + j.low) / 2;
		_journal_find_step(true);
		return;
	}

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
	_journal_find_step(true);
}

void _journal_complete_find(
    sl_status_t status, journal_index_t index, sl_sleeptimer_timestamp_t ts
) {
	journal_lower_bound_callback_t callback;
	void                          *user_data;
	CORE_ATOMIC_SECTION({
		callback        = j.find_callback;
		user_data       = j.user_data;
		j.operation     = journal_op_none;
		j.find_callback = NULL;
		j.user_data     = NULL;
	});
	if (callback == NULL) {
		return;
	}
	callback(status, index, ts, user_data);

	_journal_may_start_write();
}

sl_status_t journal_init(SPIDRV_Handle_t spi) {
	sl_status_t status = spiflash_init(spi);
	if (status != SL_STATUS_OK) {
		app_log_error("[journal] could not initialize spiflash");
		return status;
	}

	j.queue.head     = 0;
	j.queue.tail     = 0;
	j.operation      = journal_op_none;
	// this will prevent any write before initialization;
	j.next_index     = JOURNAL_INDEX_NPOS;
	j.last_timestamp = UINT32_MAX;

	return journal_find_last_before(
	    UINT32_MAX,
	    &_journal_on_find_next_idx,
	    NULL
	);
}

void _journal_on_find_next_idx(
    sl_status_t               status,
    journal_index_t           index,
    sl_sleeptimer_timestamp_t timestamp,
    void                     *user_data
) {
	(void)status;
	(void)user_data;
	if (status != SL_STATUS_OK && status != SL_STATUS_EMPTY) {
		app_log_error(
		    "[journal] could not find starting index from flash: "
		    "0x%04lX." APP_LOG_NL,
		    status
		);
		return;
	}
	j.last_timestamp = timestamp;
	j.next_index     = index + 1;
	app_log_error(
	    "[journal] found next index at %ld for times > %ld." APP_LOG_NL,
	    j.next_index,
	    j.last_timestamp
	);
}
