#include "spiflash.h"
#include "app_log.h"
#include "ecode.h"
#include "sl_core.h"
#include "sl_enum.h"
#include "sl_gpio.h"
#include "sl_sleeptimer.h"
#include "sl_status.h"
#include "spidrv.h"
#include <stdint.h>

SL_ENUM(_spiflash_operation_t){
    _spiflash_op_none = 0x00,
    _spiflash_op_read,
    _spiflash_op_write,
    _spiflash_op_erase,
    _spiflash_op_deepsleep,
};

SL_ENUM(_spiflash_polling_mode_t){
    _spiflash_poll_none = 0x00,
    _spiflash_poll_wel_set,
    _spiflash_poll_wip_cleared,
};

struct spiflash_handle {
	SPIDRV_Handle_t spi;
	sl_gpio_t       cs_pin;
	volatile bool   in_deepsleep;

	sl_sleeptimer_timer_handle_t timer;

	uint8_t                        command_buffer[5];
	volatile _spiflash_operation_t operation;
	spiflash_op_callback_t         callback;
	void                          *user_data;
	void                          *buffer;
	uint32_t                       address;
	uint32_t                       length;

	_spiflash_polling_mode_t poll;
	uint32_t                 poll_ticks;
};

static struct spiflash_handle self;

SL_ENUM(spiflash_command_t){
    mx25_cmd_read_id                = 0x9f,
    mx25_cmd_read                   = 0x03,
    mx25_cmd_wren                   = 0x06,
    mx25_cmd_read_status_register   = 0x05,
    mx25_cmd_program_page           = 0x02,
    mx25_cmd_sector_erase           = 0x20,
    mx25_cmd_sector_block_erase_32k = 0x52,
    mx25_cmd_sector_block_erase_64k = 0xd8,
    mx25_cmd_sector_chip_erase      = 0x60,
    mx25_cmd_deep_sleep             = 0xb9,
};

#define _spiflash_CS_low()                                                     \
	do {                                                                       \
		sl_gpio_clear_pin(&self.cs_pin);                                       \
	} while (0)

#define _spiflash_CS_high()                                                    \
	do {                                                                       \
		sl_gpio_set_pin(&self.cs_pin);                                         \
	} while (0)

void _spiflash_complete_op(sl_status_t status);
void _spiflash_on_wakeup_timeout(
    sl_sleeptimer_timer_handle_t *timer, void *user_data
);
void _spiflash_on_poll_timeout(
    sl_sleeptimer_timer_handle_t *timer, void *user_data
);
void _spiflash_on_deep_sleep_timeout(
    sl_sleeptimer_timer_handle_t *timer, void *user_data
);

sl_status_t _spiflash_op_action(bool call_callback);
sl_status_t _spiflash_check_erase(uint32_t address, uint32_t len);
void        _spiflash_poll(_spiflash_polling_mode_t mode, uint32_t ticks);

void _spiflash_on_cmd_done(
    SPIDRV_HandleData_t *handle, Ecode_t transferStatus, int items
);
void _spiflash_on_cmd_sent(
    SPIDRV_HandleData_t *handle, Ecode_t transferStatus, int items
);
void _spiflash_on_wren_sent(
    SPIDRV_HandleData_t *handle, Ecode_t transferStatus, int items
);

void _spiflash_on_poll_status(
    SPIDRV_HandleData_t *handle, Ecode_t transferStatus, int items
);

uint8_t _spiflash_erase_config();

void _spiflash_on_wel_set();
void _spiflash_on_wip_cleared();

sl_status_t _spiflash_deep_sleep_blocking() {
	// TODO: implement
	return SL_STATUS_OK;
}

sl_status_t _spiflash_read_id_blocking(uint8_t *buffer, uint8_t len) {
	if (len < 3) {
		return SL_STATUS_INVALID_COUNT;
	}
	_spiflash_CS_low();
	buffer[0]          = mx25_cmd_read_id;
	sl_status_t status = SPIDRV_MTransmitB(self.spi, buffer, 1);
	if (status != SL_STATUS_OK) {
		_spiflash_CS_high();
		return status;
	}

	status = SPIDRV_MReceiveB(self.spi, buffer, 3);
	_spiflash_CS_high();
	return status;
}

sl_status_t spiflash_init(SPIDRV_Handle_t spi) {
	if (spi == NULL) {
		return SL_STATUS_NULL_POINTER;
	}
	self.spi          = spi;
	self.cs_pin.port  = spi->portCs;
	self.cs_pin.pin   = spi->pinCs;
	self.in_deepsleep = false;

	self.operation = _spiflash_op_none;
	self.callback  = NULL;
	self.user_data = NULL;

	self.poll       = _spiflash_poll_none;
	self.poll_ticks = 0;

	sl_gpio_set_pin_mode(&self.cs_pin, SL_GPIO_PIN_DIRECTION_OUT, false);
	_spiflash_CS_high();

	uint8_t     buffer[3];
	sl_status_t status = _spiflash_read_id_blocking(buffer, 3);
	if (status != SL_STATUS_OK) {
		app_log_error(
		    "COuld not find mx25 device on SPI bus: 0x%04lX." APP_LOG_NL,
		    status
		);
		return status;
	}
	if (buffer[0] != 0xC2 || buffer[1] != 0x28 || buffer[2] != 0x14) {
		app_log_error(
		    "Found unexpected device ID. ManufacturerID: %02X (ex.: C2) "
		    "MemoryType: %02X (ex.: 28) MemomryDensity: %02X (ex.: "
		    "14)." APP_LOG_NL,
		    buffer[0],
		    buffer[1],
		    buffer[2]
		);
		return SL_STATUS_INVALID_SIGNATURE;
	}

	// TODO: verify status and WEL

	return SL_STATUS_OK;
}

sl_status_t _spiflash_wakeup() {
	_spiflash_CS_low();
	__NOP();
	__NOP();
	_spiflash_CS_high();
	return sl_sleeptimer_start_timer(
	    &self.timer,
	    2,
	    _spiflash_on_wakeup_timeout,
	    NULL,
	    0,
	    0
	);
}

#define SPIFLASH_MAX_ADDRESS 0x0FFFFF

sl_status_t _spiflash_start_op(
    _spiflash_operation_t  op,
    uint32_t               address,
    uint8_t               *buffer,
    uint32_t               length,
    spiflash_op_callback_t callback,
    void                  *user_data
) {
	bool in_deepsleep;
	CORE_DECLARE_IRQ_STATE;
	CORE_ENTER_ATOMIC();
	if (self.operation != _spiflash_op_none) {
		CORE_EXIT_ATOMIC();
		return SL_STATUS_BUSY;
	}
	in_deepsleep   = self.in_deepsleep;
	self.operation = op;
	self.callback  = callback;
	self.user_data = user_data;
	self.address   = address;
	self.buffer    = buffer;
	self.length    = length;
	CORE_EXIT_ATOMIC();

	sl_status_t status;

	if (in_deepsleep == true) {
		status = _spiflash_wakeup();
	} else {
		status = _spiflash_op_action(false);
	}
	if (status != SL_STATUS_OK) {
		CORE_ENTER_ATOMIC();
		self.operation = _spiflash_op_none;
		self.callback  = NULL;
		self.user_data = NULL;
		CORE_EXIT_ATOMIC();
	}
	return status;
}

sl_status_t spiflash_read(
    uint32_t               address,
    uint8_t               *buffer,
    uint32_t               len,
    spiflash_op_callback_t callback,
    void                  *user_data
) {
	if (address >= SPIFLASH_MAX_ADDRESS) {
		return SL_STATUS_INVALID_RANGE;
	}
	return _spiflash_start_op(
	    _spiflash_op_read,
	    address,
	    buffer,
	    len,
	    callback,
	    user_data
	);
}

sl_status_t spiflash_write(
    uint32_t               address,
    const uint8_t         *buffer,
    uint32_t               len,
    spiflash_op_callback_t callback,
    void                  *user_data
) {
	if (address >= SPIFLASH_MAX_ADDRESS) {
		return SL_STATUS_INVALID_RANGE;
	}
	if ((address & 0xff) != ((address + len) & 0xff)) {
		// we will cross a sector boundary!!!
		return SL_STATUS_INVALID_PARAMETER;
	}
	return _spiflash_start_op(
	    _spiflash_op_write,
	    address,
	    (uint8_t *)buffer,
	    len,
	    callback,
	    user_data
	);
}

sl_status_t spiflash_erase(
    uint32_t               address,
    uint32_t               len,
    spiflash_op_callback_t callback,
    void                  *user_data
) {
	sl_status_t status = _spiflash_check_erase(address, len);
	if (status != SL_STATUS_OK) {
		return status;
	}
	return _spiflash_start_op(
	    _spiflash_op_erase,
	    address,
	    NULL,
	    len,
	    callback,
	    user_data
	);
}

void _spiflash_on_wakeup_timeout(
    sl_sleeptimer_timer_handle_t *timer, void *user_data
) {
	(void)timer;
	(void)user_data;

	if (self.operation == _spiflash_op_none) {
		app_log_error("[spiflash] spurious wakeup" APP_LOG_NL);
		return;
	}
	_spiflash_op_action(true);
}

sl_status_t _spiflash_op_action(bool call_callback) {
	Ecode_t err;
	switch (self.operation) {
	case _spiflash_op_none:
		app_log_error("[spiflash] no operation to start." APP_LOG_NL);
		return SL_STATUS_INVALID_CONFIGURATION;
	case _spiflash_op_read:
		_spiflash_CS_low();
		self.command_buffer[0] = mx25_cmd_read;
		self.command_buffer[1] = (self.address >> 16) & 0xFF;
		self.command_buffer[2] = (self.address >> 8) & 0xFF;
		self.command_buffer[3] = (self.address >> 0) & 0xFF;
		err                    = SPIDRV_MTransmit(
            self.spi,
            self.command_buffer,
            4,
            &_spiflash_on_cmd_sent
        );
		break;
	case _spiflash_op_deepsleep:
		_spiflash_CS_low();
		self.command_buffer[0] = mx25_cmd_deep_sleep;
		err                    = SPIDRV_MTransmit(
            self.spi,
            self.command_buffer,
            1,
            &_spiflash_on_cmd_sent
        );
		break;
	case _spiflash_op_write:
	case _spiflash_op_erase:
		_spiflash_CS_low();
		self.command_buffer[0] = mx25_cmd_wren;
		err                    = SPIDRV_MTransmit(
            self.spi,
            self.command_buffer,
            1,
            &_spiflash_on_wren_sent
        );
		break;
	default:
		_spiflash_complete_op(SL_STATUS_INVALID_STATE);
		return SL_STATUS_INVALID_STATE;
	};

	if (err == ECODE_EMDRV_SPIDRV_OK) {
		return SL_STATUS_OK;
	}

	_spiflash_CS_high();
	if (call_callback == true) {
		_spiflash_complete_op(SL_STATUS_BUS_ERROR);
	}

	return SL_STATUS_BUS_ERROR;
}

void _spiflash_on_wren_sent(
    SPIDRV_HandleData_t *handle, Ecode_t transferStatus, int items
) {
	(void)handle;
	(void)items;
	_spiflash_CS_high();

	if (transferStatus != ECODE_EMDRV_SPIDRV_OK) {
		_spiflash_complete_op(SL_STATUS_TRANSMIT);
		return;
	}
	_spiflash_poll(_spiflash_poll_wel_set, 10);
}

void _spiflash_poll(_spiflash_polling_mode_t mode, uint32_t ticks) {
	self.poll          = mode;
	self.poll_ticks    = ticks;
	sl_status_t status = sl_sleeptimer_start_timer(
	    &self.timer,
	    ticks,
	    &_spiflash_on_poll_timeout,
	    NULL,
	    0,
	    0
	);
	if (status != SL_STATUS_OK) {
		_spiflash_complete_op(SL_STATUS_INITIALIZATION);
	}
}

void _spiflash_on_poll_timeout(
    sl_sleeptimer_timer_handle_t *timer, void *user_data
) {
	(void)timer;
	(void)user_data;

	self.command_buffer[0] = mx25_cmd_read_status_register;
	_spiflash_CS_low();
	Ecode_t err = SPIDRV_MTransfer(
	    self.spi,
	    self.command_buffer,
	    &self.command_buffer[1],
	    2,
	    _spiflash_on_poll_status
	);
	if (err != ECODE_EMDRV_SPIDRV_OK) {
		_spiflash_complete_op(SL_STATUS_BUS_ERROR);
	}
}

void _spiflash_on_poll_status(
    SPIDRV_HandleData_t *handle, Ecode_t transferStatus, int items
) {
	(void)handle;
	(void)items;
	_spiflash_CS_high();

	if (transferStatus != ECODE_EMDRV_SPIDRV_OK) {
		_spiflash_complete_op(SL_STATUS_TRANSMIT);
	}
	switch (self.poll) {
	case _spiflash_poll_wel_set:
		if ((self.command_buffer[1] & 0x02) == 0x00) {
			_spiflash_poll(self.poll, self.poll_ticks);
			return;
		} else {
			_spiflash_on_wel_set();
		}
		break;
	case _spiflash_poll_wip_cleared:
		if ((self.command_buffer[1] & 0x01) == 0x01) {
			_spiflash_poll(self.poll, self.poll_ticks);
			return;
		} else {
			_spiflash_on_wip_cleared();
		}
		break;
	case _spiflash_poll_none:
		app_log_error("[spiflash] spurious poll status call." APP_LOG_NL);
		return;
	default:
		_spiflash_complete_op(SL_STATUS_INVALID_STATE);
		return;
	}
}

void _spiflash_on_wel_set() {
	Ecode_t err;
	switch (self.operation) {
	case _spiflash_op_write:
		self.command_buffer[0] = mx25_cmd_program_page;
		self.command_buffer[1] = (self.address >> 16) & 0xff;
		self.command_buffer[2] = (self.address >> 8) & 0xff;
		self.command_buffer[3] = (self.address >> 0) & 0xff;
		_spiflash_CS_low();
		self.poll_ticks = 3 * self.length;
		err             = SPIDRV_MTransmit(
            self.spi,
            self.command_buffer,
            4,
            &_spiflash_on_cmd_sent
        );
		break;
	case _spiflash_op_erase: {

		uint8_t len = _spiflash_erase_config();
		_spiflash_CS_low();
		err = SPIDRV_MTransmit(
		    self.spi,
		    self.command_buffer,
		    len,
		    &_spiflash_on_cmd_sent
		);
		break;
	}
	case _spiflash_op_read:
	case _spiflash_op_deepsleep:
		_spiflash_complete_op(SL_STATUS_INVALID_STATE);
		return;
	case _spiflash_op_none:
		app_log_error("[spiflash] spurious on_wel_set call." APP_LOG_NL);
		return;
	default:
		_spiflash_complete_op(SL_STATUS_INVALID_CONFIGURATION);
		return;
	};

	if (err == ECODE_EMDRV_SPIDRV_OK) {
		return;
	}
	_spiflash_CS_high();
	_spiflash_complete_op(SL_STATUS_BUS_ERROR);
}

void _spiflash_on_cmd_sent(
    SPIDRV_HandleData_t *handle, Ecode_t transferStatus, int items
) {
	(void)handle;
	(void)items;
	if (transferStatus != ECODE_EMDRV_SPIDRV_OK) {
		_spiflash_CS_high();
		_spiflash_complete_op(SL_STATUS_TRANSMIT);
		return;
	}

	Ecode_t err;
	switch (self.operation) {
	case _spiflash_op_none:
		app_log_warning(
		    "[spiflash] spurious operation sent callback." APP_LOG_NL
		);
		return;
	case _spiflash_op_read:
		err = SPIDRV_MReceive(
		    self.spi,
		    self.buffer,
		    self.length,
		    &_spiflash_on_cmd_done
		);
		break;
	case _spiflash_op_erase:
	case _spiflash_op_deepsleep:
		_spiflash_on_cmd_done(NULL, ECODE_EMDRV_SPIDRV_OK, 0);
		return;
	case _spiflash_op_write:
		self.poll_ticks = 3 * self.length;
		err             = SPIDRV_MTransmit(
            self.spi,
            self.buffer,
            self.length,
            &_spiflash_on_cmd_done
        );
		break;
	default:
		_spiflash_CS_high();
		_spiflash_complete_op(SL_STATUS_INVALID_CONFIGURATION);
		return;
	}
	if (err != ECODE_EMDRV_SPIDRV_OK) {
		_spiflash_CS_high();
		_spiflash_complete_op(SL_STATUS_BUS_ERROR);
	}
}

void _spiflash_on_cmd_done(
    SPIDRV_HandleData_t *handle, Ecode_t transferStatus, int items
) {
	(void)handle;
	(void)items;
	_spiflash_CS_high();
	if (transferStatus != ECODE_EMDRV_SPIDRV_OK) {
		_spiflash_complete_op(SL_STATUS_TRANSMIT);
		return;
	}
	switch (self.operation) {
	case _spiflash_op_none:
		app_log_warning(
		    "[spiflash] spurious operation done callback." APP_LOG_NL
		);
		return;
	case _spiflash_op_read:
		_spiflash_complete_op(SL_STATUS_OK);
		return;
	case _spiflash_op_erase:
	case _spiflash_op_write:
		_spiflash_poll(_spiflash_poll_wip_cleared, self.poll_ticks);
		break;
	case _spiflash_op_deepsleep: {
		sl_status_t status = sl_sleeptimer_start_timer(
		    &self.timer,
		    3,
		    &_spiflash_on_deep_sleep_timeout,
		    NULL,
		    0,
		    0
		);
		if (status != SL_STATUS_OK) {
			_spiflash_complete_op(SL_STATUS_INITIALIZATION);
			return;
		}
		break;
	}
	default:
		_spiflash_complete_op(SL_STATUS_INVALID_CONFIGURATION);
	}
}

void _spiflash_on_wip_cleared() {
	switch (self.operation) {
	case _spiflash_op_none:
		app_log_warning("[spiflash] spurious wip cleared callback" APP_LOG_NL);
		return;
	case _spiflash_op_read:
	case _spiflash_op_deepsleep:
		_spiflash_complete_op(SL_STATUS_INVALID_STATE);
		break;
	case _spiflash_op_erase:
	case _spiflash_op_write:
		_spiflash_complete_op(SL_STATUS_OK);
		break;
	default:
		_spiflash_complete_op(SL_STATUS_INVALID_CONFIGURATION);
	}
}

uint8_t _spiflash_erase_config() {
	switch (self.length) {
	case SPIFLASH_4K:
		self.command_buffer[0] = mx25_cmd_sector_erase;
		self.command_buffer[1] = (self.address >> 16) & 0xff;
		self.command_buffer[2] = (self.address >> 8) & 0xff;
		self.command_buffer[3] = (self.address >> 0) & 0xff;
		self.poll_ticks        = sl_sleeptimer_ms_to_tick(60);
		return 4;
	case SPIFLASH_32K:
		self.command_buffer[0] = mx25_cmd_sector_block_erase_32k;
		self.command_buffer[1] = (self.address >> 16) & 0xff;
		self.command_buffer[2] = (self.address >> 8) & 0xff;
		self.command_buffer[3] = (self.address >> 0) & 0xff;
		sl_sleeptimer_ms32_to_tick(400, &self.poll_ticks);
		return 4;
	case SPIFLASH_64K:
		self.command_buffer[0] = mx25_cmd_sector_block_erase_64k;
		self.command_buffer[1] = (self.address >> 16) & 0xff;
		self.command_buffer[2] = (self.address >> 8) & 0xff;
		self.command_buffer[3] = (self.address >> 0) & 0xff;
		sl_sleeptimer_ms32_to_tick(800, &self.poll_ticks);
		return 4;
	case SPIFLASH_1M:
		self.command_buffer[0] = mx25_cmd_sector_chip_erase;
		sl_sleeptimer_ms32_to_tick(15000, &self.poll_ticks);
		return 1;
	default:
		return 0;
	}
}

void _spiflash_complete_op(sl_status_t status) {
	spiflash_op_callback_t callback;
	void                  *user_data;
	CORE_ATOMIC_SECTION({
		callback       = self.callback;
		user_data      = self.user_data;
		self.operation = _spiflash_op_none;
		self.callback  = NULL;
		self.user_data = NULL;
	});

	if (callback == NULL) {
		return;
	}
	callback(status, user_data);
}

sl_status_t _spiflash_check_erase(uint32_t address, uint32_t length) {
	// check size
	switch (length) {
	case SPIFLASH_4K:
	case SPIFLASH_32K:
	case SPIFLASH_64K:
	case SPIFLASH_1M:
		break;
	default:
		return SL_STATUS_INVALID_COUNT;
	}
	// check alignement
	if ((address & (length - 1)) != 0) {
		return SL_STATUS_INVALID_PARAMETER;
	}
	return SL_STATUS_OK;
}

void _spiflash_on_deep_sleep_timeout(
    sl_sleeptimer_timer_handle_t *timer, void *user_data
) {
	(void)timer;
	(void)user_data;
	CORE_ATOMIC_SECTION({ self.in_deepsleep = true; })
	_spiflash_complete_op(SL_STATUS_OK);
}

sl_status_t
spiflash_enter_deepsleep(spiflash_op_callback_t callback, void *user_data) {
	CORE_DECLARE_IRQ_STATE;
	CORE_ENTER_ATOMIC();
	if (self.in_deepsleep == true) {
		CORE_EXIT_ATOMIC();
		return SL_STATUS_OK;
	}
	sl_status_t status = _spiflash_start_op(
	    _spiflash_op_deepsleep,
	    0,
	    NULL,
	    0,
	    callback,
	    user_data
	);
	CORE_EXIT_ATOMIC();
	return status;
}
