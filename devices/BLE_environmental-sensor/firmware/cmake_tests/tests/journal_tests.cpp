#include <cstdint>
#include <cstring>

#include <span>

#include <gtest/gtest.h>

#include "journal.h"
#include "journal_priv.h"
#include "journal_record.h"
#include "sl_sleeptimer.h"
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
		size_t i = 0;
		for (const auto &r : records) {
			uint8_t *record_buffer = &memory[i++ * sizeof(journal_record_t)];
			memset(
			    record_buffer + sizeof(journal_record_header_t),
			    0x01,
			    sizeof(journal_record_t) - sizeof(journal_record_data_t)
			);
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
		do {
			std::this_thread::yield();
		} while (j.operation != journal_op_none);
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

	journal_find_result found;
	EXPECT_EQ(journal_find_last_before(8, &on_find, &found), SL_STATUS_EMPTY);

	data_point_t dp = {
	    .date = 10,
	};

	sl_status_t status = journal_add_record(&dp);
	EXPECT_EQ(status, SL_STATUS_OK);
	waitNotBusy();

	EXPECT_EQ(j.next_index, 1);
	EXPECT_EQ(j.last_timestamp, 10);
	EXPECT_EQ(j.first_index, 0);
	EXPECT_EQ(j.first_timestamp, 10);

	EXPECT_EQ(journal_find_last_before(11, &on_find, &found), SL_STATUS_OK);
	waitNotBusy();
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
	EXPECT_EQ(j.next_index, 2);
	EXPECT_EQ(j.last_timestamp, 0);
	EXPECT_EQ(j.first_index, JOURNAL_INDEX_NPOS);
	EXPECT_EQ(j.first_timestamp, UINT32_MAX);

	journal_find_result found;
	EXPECT_EQ(journal_find_last_before(8, &on_find, &found), SL_STATUS_EMPTY);

	data_point_t dp = {
	    .date = 10,
	};

	sl_status_t status = journal_add_record(&dp);
	EXPECT_EQ(status, SL_STATUS_OK);
	waitNotBusy();

	EXPECT_EQ(j.next_index, 3);
	EXPECT_EQ(j.last_timestamp, 10);
	EXPECT_EQ(j.first_index, 2);
	EXPECT_EQ(j.first_timestamp, 10);

	EXPECT_EQ(journal_find_last_before(11, &on_find, &found), SL_STATUS_OK);
	waitNotBusy();
	EXPECT_EQ(found.status, SL_STATUS_OK);
	EXPECT_EQ(found.index, 0);
	EXPECT_EQ(found.timestamp, 10);
}
