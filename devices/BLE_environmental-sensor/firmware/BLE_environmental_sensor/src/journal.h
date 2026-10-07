#pragma once

#include "sl_enum.h"
#include "sl_sleeptimer.h"
#include "sl_status.h"
#include "types.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif //__cplusplus

#define JOURNAL_INDEX_NPOS UINT32_MAX
#define JOURNAL_READ_CHUNK 16

// allow for injection of the JOURNAL_SIZE for unit test purposes.
#ifndef JOURNAL_SIZE
#define JOURNAL_SIZE                                                           \
	((journal_index_t)(SPIFLASH_SIZE / sizeof(journal_record_t)))
#endif // jOURNAL_SIZE

/**
 * Initialize the journal system. This will scan the flash memory for existing
 * records and set up the internal state accordingly.
 *
 * @return SL_STATUS_OK if initialization was successful, or an error code
 *         if there was a problem.
 */

sl_status_t journal_init();

/**
 * Process any pending journal operations. This function should be called
 * periodically from the main loop to ensure that journal operations are
 * completed in a timely manner.
 *
 * It has its own gate mechanism, so do not gate it behing app_proceed()
 */
void journal_process_action();

/**
 * Query whether the journal is in a state that allows the system to enter
 * sleep. i.e. no operation completion are pending.
 */
bool journal_is_ok_to_sleep();

typedef uint32_t journal_index_t;

/**
 * Asdynchronously adds a record to the journal. Note that the input is queued,
 * so it won't error if another operation is in flight.
 */
sl_status_t journal_add_record(const data_point_t *dp);

/**
 * Return the current journal record count.
 *
 * @note the enqueued record are not counted, only actually written or in-flight
 * write record.
 *
 * @warning record with bad CRC are also counted. Only a full read could
 * accurately return the number of that are stored without errors.
 */
uint16_t journal_record_count();

/**
 * Callback function type for journal_find_last_before. The callback is not
 * called from ISR.
 *
 * @param status The status of the find operation. SL_STATUS_OK if successful,
 *        or an error code.
 * @param index The index of the found record, or JOURNAL_INDEX_NPOS if not
 *        found.
 * @param timestamp The timestamp of the found record, or UINT32_MAX if not
 *        found.
 * @param user_data User-defined data passed to the callback.
 */
typedef void (*journal_find_callback_t)(
    sl_status_t               status,
    journal_index_t           index,
    sl_sleeptimer_timestamp_t timestamp,
    void                     *user_data
);

/**
 * Asynchronously finds the last record in the journal with a timestamp strictly
 * smaller than the given date. The result is provided via the callback.
 *
 * @param date The date to compare against.
 * @param callback The callback function to call with the result. Will not be
 *        called from ISR context.
 * @param user_data User-defined data to pass to the callback.
 *
 * @return SL_STATUS_OK if the find operation was successfully initiated, or an
 *         error code if there was a problem (e.g., SL_STATUS_BUSY if another
 *         find operation is already in progress).
 */
sl_status_t journal_find_last_before(
    sl_sleeptimer_timestamp_t date,
    journal_find_callback_t   callback,
    void                     *user_data
);

SL_ENUM(journal_read_next_operation_t){
    journal_read_continue = 0, // value consumed, and give another one ASAP.
    journal_read_pause =
        1, // value consumed, but wait for a resume to give another one.
    journal_read_discard = 2, // value consumed, but discard all remaining value
                              // and stop immediatly.
};

/**
 * Callback function type for journal_read. The callback is not called from ISR.
 *
 * @param status The status of the read operation. SL_STATUS_OK if successful,
 *       or an error code.
 * @param point The data point read from the journal, or NULL if the read is
 *       complete or if there was an error.
 * @param user_data User-defined data passed to the callback.
 *
 * @return the user should return what to do next with the remaining values to
 * be read.
 */
typedef journal_read_next_operation_t (*journal_read_callback_t)(
    sl_status_t status, const data_point_t *point, void *user_data
);

/**
 * Asynchronously reads records from the journal between the specified start and
 * end indices. The records are provided one by one via the callback.
 *
 * @param start The starting index of the records to read (inclusive).
 * @param end The ending index of the records to read (exclusive).
 * @param callback The callback function to call with each read record. Will
 *      not be called from ISR context.
 * @param user_data User-defined data to pass to the callback.
 */
sl_status_t journal_read(
    journal_index_t         start,
    journal_index_t         end,
    journal_read_callback_t callback,
    void                   *user_data
);

/**
 * Resume read operation ASAP, can be called from ISR.
 *
 * @return SL_STATUS_OK or an error otherwise.
 */
sl_status_t journal_read_resume();

/**
 * Abort current read operation. Best effort and some read can still be seen by
 * the user afterwards.
 *
 * @return SL_STATUS_OK or an error otherwise.
 */
sl_status_t journal_read_abord();

/**
 * Asynchronously erases all records in the journal. The operation is
 * non-blocking and will complete in the background. The journal will be empty
 * after this operation.
 *
 * @return SL_STATUS_BUSY if an operation is currently in flight. SL_STATUS_OK
 * if the operation is started (but not completed)
 */

sl_status_t journal_erase();

/**
 * Preempt the flash sleeping state. When preempt is true, the journal will
 * not enter sleep mode until set back to false.  Otherwise, the underlying
 * FLASH module is put to sleep after eac * h operation completion.
 *
 * @param preempt If true, preempt the flash from sleeping; if false, allow the
 *        journal to sleep after operations. Setting to false will put the flash
 *        to sleep if no other operations are pending.
 */
void journal_preempt_sleeping(bool preempt);

#ifdef __cplusplus
}
#endif //__cplusplus
