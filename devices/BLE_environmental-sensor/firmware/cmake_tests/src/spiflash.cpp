#include "spiflash.hpp"

#include <mutex>
#include <string.h>

#include <array>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <thread>

#include "sl_status.h"

class MockSPIFlash {
public:
	static constexpr size_t SPIFLASH_SIZE = 1024 * 1024;

	MockSPIFlash()
	    : busy(false) {
		eraseChip();
	}

	void eraseChip() {
		memset(data.data(), 0xff, SPIFLASH_SIZE);
	}

	sl_status_t read(
	    uint32_t               address,
	    uint8_t               *buffer,
	    uint32_t               length,
	    spiflash_op_callback_t callback,
	    void                  *user_data
	) {
		if (address >= SPIFLASH_SIZE || address > (SPIFLASH_SIZE - length)) {
			return SL_STATUS_INVALID_RANGE;
		}
		std::lock_guard<std::mutex> lock(mutex);
		if (busy == true) {
			return SL_STATUS_BUSY;
		}
		busy = true;

		std::thread t([this, address, buffer, length, callback, user_data]() {
			std::this_thread::yield();
			memcpy(buffer, &data[address], length);
			{
				std::lock_guard<std::mutex> lock(mutex);
				this->busy = false;
			}
			callback(SL_STATUS_OK, user_data);
		});
		t.detach();
		return SL_STATUS_OK;
	};

	sl_status_t write(
	    uint32_t               address,
	    const uint8_t         *buffer,
	    uint32_t               length,
	    spiflash_op_callback_t callback,
	    void                  *user_data
	) {
		if (address >= SPIFLASH_SIZE || address > (SPIFLASH_SIZE - length)) {
			return SL_STATUS_INVALID_RANGE;
		}
		std::lock_guard<std::mutex> lock(mutex);
		if (busy == true) {
			return SL_STATUS_BUSY;
		}
		busy = true;

		std::thread t([this, address, buffer, length, callback, user_data]() {
			std::this_thread::yield();
			memcpy(&data[address], buffer, length);
			{
				std::lock_guard<std::mutex> lock(mutex);
				this->busy = false;
			}
			callback(SL_STATUS_OK, user_data);
		});
		t.detach();
		return SL_STATUS_OK;
	};

	void setMemory(std::span<const uint8_t> bytes) {
		memcpy(data.data(), bytes.data(), bytes.size());
		memset(data.data() + bytes.size(), 0xff, SPIFLASH_SIZE - bytes.size());
		size_t end = (bytes.size() + 16) / 16;
		end *= 16;
		std::cerr << "memory content";
		for (size_t i = 0; i < end; ++i) {
			if (i % 16 == 0) {
				std::cerr << std::endl
				          << "0x" << std::setfill('0') << std::setw(6)
				          << std::hex << i << ": ";
			}
			std::cerr << std::hex << std::setw(2) << std::setfill('0')
			          << (int)data[i] << " ";
		}
		std::cerr << std::endl << "..." << std::endl;
	}

private:
	std::array<uint8_t, SPIFLASH_SIZE> data;
	std::mutex                         mutex;
	bool                               busy;
};

static MockSPIFlash spiflash;

void spiflash_set_memory(std::span<const uint8_t> bytes) {
	spiflash.setMemory(bytes);
}

extern "C" {
typedef void (*spiflash_op_callback_t)(sl_status_t, void *);

sl_status_t spiflash_read(
    uint32_t               address,
    uint8_t               *buffer,
    uint32_t               len,
    spiflash_op_callback_t callback,
    void                  *user_data
) {
	return spiflash.read(address, buffer, len, callback, user_data);
}

sl_status_t spiflash_write(
    uint32_t               address,
    const uint8_t         *buffer,
    uint32_t               len,
    spiflash_op_callback_t callback,
    void                  *user_data
) {
	return spiflash.write(address, buffer, len, callback, user_data);
}
} // __cplusplus
