#include "journal_record.h"
#include "utils/crc8.h"

uint8_t _journal_record_compute_crc(const uint8_t *buffer, uint8_t size) {
	uint8_t crc = 0xff;
	for (uint8_t i = 0; i < size; ++i) {
		crc = CRC8_AppendByte(crc, 0x31, buffer[i]);
	}
	return crc;
}

void journal_record_from_data_point(
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
	r->data.co2         = dp->co2;
	r->data.crc         = _journal_record_compute_crc(
        (uint8_t *)&r->data,
        sizeof(journal_record_data_t) - sizeof(uint8_t)
    );
}

void journal_record_to_data_point(const journal_record_t *r, data_point_t *dp) {
	dp->date        = r->header.timestamp;
	dp->temperature = r->data.temperature;
	dp->humidity    = r->data.humidity;
	dp->pressure    = r->data.pressure;
	dp->co2         = r->data.co2;
}

bool journal_record_header_check_crc(const journal_record_header_t *r) {
	return _journal_record_compute_crc(
	           (const uint8_t *)r,
	           sizeof(journal_record_header_t)
	       ) == 0;
}

bool journal_record_check_crc(const journal_record_t *r) {
	return journal_record_header_check_crc(&r->header) &&
	       _journal_record_compute_crc(
	           (const uint8_t *)&r->data,
	           sizeof(journal_record_data_t)
	       ) == 0;
}
