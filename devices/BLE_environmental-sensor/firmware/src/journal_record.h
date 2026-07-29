#pragma once

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus
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

void journal_record_from_data_point(
    journal_record_t *r, const data_point_t *dp
);
void journal_record_to_data_point(const journal_record_t *r, data_point_t *dp);

bool journal_record_header_check_crc(const journal_record_header_t *r);
bool journal_record_check_crc(const journal_record_t *r);

#ifdef __cplusplus
}
#endif // __cplusplus
