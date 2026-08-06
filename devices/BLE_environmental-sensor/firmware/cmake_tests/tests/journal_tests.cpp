#include <chrono>
#include <cstdint>
#include <cstring>
#include <future>
#include <ratio>
#include <span>
#include <stdint.h>
#include <thread>

#include <gtest/gtest.h>

#include "journal.h"
#include "journal_priv.h"
#include "journal_record.h"
#include "sl_sleeptimer.h"
#include "sl_status.h"
#include "spiflash.hpp"
#include "types.h"
#include "utils/crc8.h"

static_assert(JOURNAL_SIZE == 16, "Journal size should not be the whole FLASH");

struct RecordDescription {
	sl_sleeptimer_timestamp_t timestamp;
	bool                      bad_crc = false;
};

class JournalTest : public ::testing::Test {
protected:
	static void setMemory(std::span<const RecordDescription> records) {
		ASSERT_LE(records.size(), JOURNAL_SIZE);
		std::vector<uint8_t> memory;
		memory.resize(sizeof(journal_record_t) * records.size());
		size_t  i   = 0;
		uint8_t crc = 0xff;
		for (uint8_t i = 0; i < (sizeof(journal_record_data) - 1); ++i) {
			crc = CRC8_AppendByte(crc, 0x31, 0x00);
		}
		for (const auto &r : records) {
			uint8_t *record_buffer = &memory[i++ * sizeof(journal_record_t)];
			memset(
			    record_buffer + sizeof(journal_record_header_t),
			    0x00,
			    sizeof(journal_record_data_t) - 1
			);
			record_buffer[sizeof(journal_record_t) - 1] = crc;

			memcpy(
			    record_buffer,
			    &r.timestamp,
			    sizeof(sl_sleeptimer_timestamp_t)
			);
			record_buffer[sizeof(sl_sleeptimer_timestamp_t)] = 0xff;
			for (uint8_t i = 0; i < sizeof(sl_sleeptimer_timestamp_t); ++i) {
				record_buffer[sizeof(sl_sleeptimer_timestamp_t)] =
				    CRC8_AppendByte(
				        record_buffer[sizeof(sl_sleeptimer_timestamp_t)],
				        0x31,
				        record_buffer[i]
				    );
			}
			if (r.bad_crc == true) {
				record_buffer[sizeof(sl_sleeptimer_timestamp_t)] += 1;
			}
		}

		spiflash_set_memory(memory);
	}

	void waitNotBusy() {
		for (size_t i = 0; i < 50; ++i) {
			journal_process_action();
			if (j.operation == journal_op_none &&
			    j.operation_done == journal_op_none) {
				return;
			}
			std::cerr << "waiting for op " << (int)j.operation << " to finish"
			          << std::endl;
			std::this_thread::sleep_for(std::chrono::milliseconds{10});
		}
		if (j.operation == journal_op_none) {
			return;
		}
		ADD_FAILURE() << "timeout on operation";
	}
};

struct journal_find_result {
	sl_status_t               status;
	journal_index_t           index;
	sl_sleeptimer_timestamp_t timestamp;
};

void on_find(
    sl_status_t               status,
    journal_index_t           idx,
    sl_sleeptimer_timestamp_t ts,
    void                     *user_data
) {

	journal_find_result *res =
	    reinterpret_cast<journal_find_result *>(user_data);
	res->status    = status;
	res->index     = idx;
	res->timestamp = ts;
};

TEST_F(JournalTest, EmptyInitialization) {
	setMemory({});
	journal_init();
	waitNotBusy();
	EXPECT_EQ(j.next_index, 0);
	EXPECT_EQ(j.last_timestamp, 0);
	EXPECT_EQ(j.first_index, JOURNAL_INDEX_NPOS);
	EXPECT_EQ(j.first_timestamp, UINT32_MAX);
	EXPECT_TRUE(spiflash_sleeping());

	journal_find_result found;
	EXPECT_EQ(journal_find_last_before(8, &on_find, &found), SL_STATUS_EMPTY);

	data_point_t dp = {
	    .date = 10,
	};

	sl_status_t status = journal_add_record(&dp);
	EXPECT_EQ(status, SL_STATUS_OK);
	EXPECT_FALSE(spiflash_sleeping());
	waitNotBusy();
	EXPECT_TRUE(spiflash_sleeping());

	EXPECT_EQ(j.next_index, 1);
	EXPECT_EQ(j.last_timestamp, 10);
	EXPECT_EQ(j.first_index, 0);
	EXPECT_EQ(j.first_timestamp, 10);

	EXPECT_EQ(journal_find_last_before(11, &on_find, &found), SL_STATUS_OK);
	waitNotBusy();
	EXPECT_TRUE(spiflash_sleeping());
	EXPECT_EQ(found.status, SL_STATUS_OK);
	EXPECT_EQ(found.index, 0);
	EXPECT_EQ(found.timestamp, 10);
}

TEST_F(JournalTest, BadCRCAtFirst) {
	setMemory(std::array<RecordDescription, 2>{
	    RecordDescription{10, true},
	    RecordDescription{20, true}
	});
	journal_init();
	waitNotBusy();
	EXPECT_TRUE(spiflash_sleeping());
	EXPECT_EQ(j.next_index, 2);
	EXPECT_EQ(j.last_timestamp, 0);
	EXPECT_EQ(j.first_index, JOURNAL_INDEX_NPOS);
	EXPECT_EQ(j.first_timestamp, UINT32_MAX);

	journal_find_result found;
	EXPECT_EQ(journal_find_last_before(8, &on_find, &found), SL_STATUS_EMPTY);
	EXPECT_TRUE(spiflash_sleeping());
	data_point_t dp = {
	    .date = 10,
	};

	sl_status_t status = journal_add_record(&dp);
	EXPECT_EQ(status, SL_STATUS_OK);
	EXPECT_FALSE(spiflash_sleeping());
	waitNotBusy();
	EXPECT_TRUE(spiflash_sleeping());

	EXPECT_EQ(j.next_index, 3);
	EXPECT_EQ(j.last_timestamp, 10);
	EXPECT_EQ(j.first_index, 2);
	EXPECT_EQ(j.first_timestamp, 10);

	EXPECT_EQ(journal_find_last_before(11, &on_find, &found), SL_STATUS_OK);
	EXPECT_TRUE(spiflash_sleeping());
	waitNotBusy();
	EXPECT_TRUE(spiflash_sleeping());
	EXPECT_EQ(found.status, SL_STATUS_OK);
	EXPECT_EQ(found.index, 2);
	EXPECT_EQ(found.timestamp, 10);
}

TEST_F(JournalTest, FullBadCRC) {
	setMemory(std::array<RecordDescription, 16>{
	    RecordDescription{10, true},
	    RecordDescription{20, true},
	    RecordDescription{30, true},
	    RecordDescription{40, true},
	    RecordDescription{50, true},
	    RecordDescription{60, true},
	    RecordDescription{70, true},
	    RecordDescription{80, true},
	    RecordDescription{90, true},
	    RecordDescription{100, true},
	    RecordDescription{110, true},
	    RecordDescription{120, true},
	    RecordDescription{130, true},
	    RecordDescription{140, true},
	    RecordDescription{150, true},
	    RecordDescription{160, true},
	});
	journal_init();
	waitNotBusy();
	EXPECT_TRUE(spiflash_sleeping());

	EXPECT_EQ(j.next_index, JOURNAL_INDEX_NPOS);
	EXPECT_EQ(j.last_timestamp, UINT32_MAX);
	EXPECT_EQ(j.first_index, JOURNAL_INDEX_NPOS);
	EXPECT_EQ(j.first_timestamp, UINT32_MAX);

	journal_find_result found;
	EXPECT_EQ(journal_find_last_before(8, &on_find, &found), SL_STATUS_EMPTY);
	EXPECT_TRUE(spiflash_sleeping());
	data_point_t dp = {
	    .date = 10,
	};

	sl_status_t status = journal_add_record(&dp);
	EXPECT_EQ(status, SL_STATUS_INVALID_PARAMETER);
	EXPECT_TRUE(spiflash_sleeping());
	waitNotBusy();
	EXPECT_TRUE(spiflash_sleeping());

	EXPECT_EQ(journal_find_last_before(11, &on_find, &found), SL_STATUS_EMPTY);
}

journal_read_next_operation_t
on_read(sl_status_t status, const data_point_t *dp, void *user_data) {
	sl_sleeptimer_timestamp_t *expected =
	    reinterpret_cast<sl_sleeptimer_timestamp_t *>(user_data);
	EXPECT_EQ(status, SL_STATUS_OK);
	if (*expected == 170) {
		EXPECT_EQ(dp, nullptr);
		return journal_read_continue;
	}
	if (dp == nullptr) {
		ADD_FAILURE() << "early termination " << *expected;
		return journal_read_discard;
	}
	EXPECT_EQ(dp->date, *expected);
	*expected += 10;
	return journal_read_continue;
}

TEST_F(JournalTest, CannotPutMoreThanSize) {
	setMemory({});
	journal_init();
	waitNotBusy();
	EXPECT_TRUE(spiflash_sleeping());

	EXPECT_EQ(j.next_index, 0);
	EXPECT_EQ(j.last_timestamp, 0);
	EXPECT_EQ(j.first_index, JOURNAL_INDEX_NPOS);
	EXPECT_EQ(j.first_timestamp, UINT32_MAX);

	data_point_t dp;

	for (uint8_t i = 0; i < 16; ++i) {
		dp.date = 10 * (i + 1);
		SCOPED_TRACE("for index " + std::to_string(i));
		EXPECT_EQ(journal_add_record(&dp), SL_STATUS_OK);
	}
	EXPECT_FALSE(spiflash_sleeping());
	dp.date = 170;
	EXPECT_EQ(journal_add_record(&dp), SL_STATUS_FULL);
	EXPECT_FALSE(spiflash_sleeping());
	waitNotBusy();
	EXPECT_TRUE(spiflash_sleeping());

	sl_sleeptimer_timestamp_t expected{10};
	EXPECT_EQ(journal_read(0, 16, &on_read, &expected), SL_STATUS_OK);
	EXPECT_FALSE(spiflash_sleeping());
	waitNotBusy();
	EXPECT_EQ(expected, 170);
	EXPECT_TRUE(spiflash_sleeping());
}

TEST_F(JournalTest, PartiallySet) {
	setMemory(std::array<RecordDescription, 4>{
	    RecordDescription{10},
	    RecordDescription{20},
	    RecordDescription{30},
	    RecordDescription{40},
	});
	journal_init();
	waitNotBusy();
	EXPECT_TRUE(spiflash_sleeping());
	EXPECT_EQ(j.next_index, 4);
	EXPECT_EQ(j.last_timestamp, 40);
	EXPECT_EQ(j.first_index, 0);
	EXPECT_EQ(j.first_timestamp, 10);

	journal_find_result found;
	EXPECT_EQ(journal_find_last_before(21, &on_find, &found), SL_STATUS_OK);
	EXPECT_FALSE(spiflash_sleeping());
	waitNotBusy();
	EXPECT_TRUE(spiflash_sleeping());
	EXPECT_EQ(found.status, SL_STATUS_OK);
	EXPECT_EQ(found.index, 1);
	EXPECT_EQ(found.timestamp, 20);
}

TEST_F(JournalTest, BadCRCatEnd) {
	setMemory(std::array<RecordDescription, 16>{
	    RecordDescription{20},
	    RecordDescription{30},
	    RecordDescription{40},
	    RecordDescription{50},
	    RecordDescription{60},
	    RecordDescription{70},
	    RecordDescription{80},
	    RecordDescription{90},
	    RecordDescription{100},
	    RecordDescription{110},
	    RecordDescription{120},
	    RecordDescription{130},
	    RecordDescription{140},
	    RecordDescription{150},
	    RecordDescription{160},
	    RecordDescription{170, true},
	});
	journal_init();
	waitNotBusy();
	EXPECT_TRUE(spiflash_sleeping());

	EXPECT_EQ(j.next_index, 16);
	EXPECT_EQ(j.last_timestamp, 160);
	EXPECT_EQ(j.first_index, 0);
	EXPECT_EQ(j.first_timestamp, 20);

	sl_sleeptimer_timestamp_t expected{20};
	EXPECT_EQ(journal_read(0, 16, &on_read, &expected), SL_STATUS_OK);
	EXPECT_FALSE(spiflash_sleeping());
	waitNotBusy();
	EXPECT_EQ(expected, 170);
	EXPECT_TRUE(spiflash_sleeping());
}

TEST_F(JournalTest, BadCRCinMiddle) {
	setMemory(std::array<RecordDescription, 8>{
	    RecordDescription{10},
	    RecordDescription{.timestamp = 20, .bad_crc = true},
	    RecordDescription{.timestamp = 30, .bad_crc = true},
	    RecordDescription{.timestamp = 40, .bad_crc = true},
	    RecordDescription{.timestamp = 50, .bad_crc = true},
	    RecordDescription{.timestamp = 60, .bad_crc = true},
	    RecordDescription{.timestamp = 70, .bad_crc = true},
	    RecordDescription{80},
	});
	journal_init();
	waitNotBusy();
	EXPECT_TRUE(spiflash_sleeping());
	EXPECT_EQ(j.next_index, 8);
	EXPECT_EQ(j.last_timestamp, 80);
	EXPECT_EQ(j.first_index, 0);
	EXPECT_EQ(j.first_timestamp, 10);

	journal_find_result found;
	EXPECT_EQ(journal_find_last_before(85, &on_find, &found), SL_STATUS_OK);
	EXPECT_TRUE(spiflash_sleeping()); // not reading flash
	waitNotBusy();
	EXPECT_TRUE(spiflash_sleeping());
	EXPECT_EQ(found.status, SL_STATUS_OK);
	EXPECT_EQ(found.timestamp, 80);
	EXPECT_EQ(found.index, 7);

	EXPECT_EQ(journal_find_last_before(30, &on_find, &found), SL_STATUS_OK);
	EXPECT_FALSE(spiflash_sleeping());
	waitNotBusy();
	EXPECT_TRUE(spiflash_sleeping());
	EXPECT_EQ(found.status, SL_STATUS_OK);
	EXPECT_EQ(found.timestamp, 10);
	EXPECT_EQ(found.index, 0);
}

TEST_F(JournalTest, SleepingPreemption) {
	setMemory({});
	journal_init();
	waitNotBusy();
	EXPECT_TRUE(spiflash_sleeping());
	journal_preempt_sleeping(true);

	data_point_t dp{.date = 10};

	EXPECT_EQ(journal_add_record(&dp), SL_STATUS_OK);
	EXPECT_FALSE(spiflash_sleeping());
	waitNotBusy();
	EXPECT_FALSE(spiflash_sleeping()); // here sleeping is preempted.

	journal_preempt_sleeping(false);
	waitNotBusy();
	EXPECT_TRUE(spiflash_sleeping()); // here sleeping is preempted.

	journal_preempt_sleeping(true);
	dp.date = 20;
	EXPECT_EQ(journal_add_record(&dp), SL_STATUS_OK);
	EXPECT_FALSE(spiflash_sleeping());
	journal_preempt_sleeping(false);
	EXPECT_FALSE(spiflash_sleeping());
	waitNotBusy();
	EXPECT_TRUE(spiflash_sleeping()); // here sleeping is preempted.
}
