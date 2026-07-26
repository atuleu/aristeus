#include "i2c_schd.h"
#include "sl_core.h"
#include "sl_i2c.h"
#include "sl_power_manager.h"
#include "sl_sleeptimer.h"
#include "sl_status.h"
#include <stdint.h>

sl_status_t i2c_schd_receive_blocking(
    i2c_schd_handle_t *self, uint8_t address, uint8_t *buffer, uint8_t len
) {
	if (self == NULL || buffer == NULL) {
		return SL_STATUS_NULL_POINTER;
	}
	CORE_DECLARE_IRQ_STATE;
	CORE_ENTER_ATOMIC();
	if (self->current_tx != NULL) {
		CORE_EXIT_ATOMIC();
		return SL_STATUS_BUSY;
	}
	CORE_EXIT_ATOMIC();

	return sl_i2c_leader_receive_blocking(
	    self->i2c_bus,
	    address,
	    buffer,
	    len,
	    len + 1
	);
}

sl_status_t i2c_schd_send_blocking(
    i2c_schd_handle_t *self, uint8_t address, const uint8_t *buffer, uint8_t len
) {
	if (self == NULL || buffer == NULL) {
		return SL_STATUS_NULL_POINTER;
	}

	CORE_DECLARE_IRQ_STATE;
	CORE_ENTER_ATOMIC();
	if (self->current_tx != NULL) {
		CORE_EXIT_ATOMIC();
		return SL_STATUS_BUSY;
	}
	CORE_EXIT_ATOMIC();

	return sl_i2c_leader_send_blocking(
	    self->i2c_bus,
	    address,
	    buffer,
	    len,
	    len + 1
	);
}

sl_status_t i2c_schd_transfer_blocking(
    i2c_schd_handle_t *self,
    uint8_t            address,
    const uint8_t     *write_buffer,
    uint8_t            write_len,
    uint8_t           *read_buffer,
    uint8_t            read_len
) {
	if (self == NULL || write_buffer == NULL || read_buffer == NULL) {
		return SL_STATUS_NULL_POINTER;
	}

	CORE_DECLARE_IRQ_STATE;
	CORE_ENTER_ATOMIC();
	if (self->current_tx != NULL) {
		CORE_EXIT_ATOMIC();
		return SL_STATUS_BUSY;
	}
	CORE_EXIT_ATOMIC();

	return sl_i2c_leader_transfer_blocking(
	    self->i2c_bus,
	    address,
	    write_buffer,
	    write_len,
	    read_buffer,
	    read_len,
	    write_len + read_len + 2
	);
}

bool _i2c_sched_queue_empty(i2c_schd_handle_t *self) {
	return self->head == self->tail;
}

static_assert(
    I2C_SCHEDULER_QUEUE_SIZE > 0,
    "I2C_SCHEDULER_QUEUE_SIZE must be greater than 0"
);
static_assert(
    (I2C_SCHEDULER_QUEUE_SIZE & (I2C_SCHEDULER_QUEUE_SIZE - 1)) == 0,
    "I2C_SCHEDULER_QUEUE_SIZE must be a power of 2"
);

#define I2C_SCHEDULER_QUEUE_MASK (I2C_SCHEDULER_QUEUE_SIZE - 1)

bool _i2c_sched_queue_full(i2c_schd_handle_t *self) {
	return self->head == ((self->tail + 1) & I2C_SCHEDULER_QUEUE_MASK);
}

sl_status_t _i2c_schd_transfer(
    i2c_schd_handle_t *self,
    uint8_t            address,
    const uint8_t     *write_buffer,
    uint8_t            write_len,
    uint8_t           *read_buffer,
    uint8_t            read_len,
    i2c_tx_callback_t  callback,
    void              *user_data
) {

	if (callback == NULL) {
		return SL_STATUS_NULL_POINTER;
	}

	CORE_DECLARE_IRQ_STATE;
	CORE_ENTER_ATOMIC();
	if (_i2c_sched_queue_full(self)) {
		CORE_EXIT_ATOMIC();
		return SL_STATUS_BUSY;
	}
	i2c_tx_handle_t *tx = &self->queue[self->tail];
	self->tail          = (self->tail + 1) & I2C_SCHEDULER_QUEUE_MASK;
	tx->address         = address;
	tx->write_buffer    = write_buffer;
	tx->write_len       = write_len;
	tx->read_buffer     = read_buffer;
	tx->read_len        = read_len;
	tx->callback        = callback;
	tx->user_data       = user_data;
	CORE_EXIT_ATOMIC();
	return SL_STATUS_OK;
}

sl_status_t i2c_schd_transfer(
    i2c_schd_handle_t *self,
    uint8_t            address,
    const uint8_t     *write_buffer,
    uint8_t            write_len,
    uint8_t           *read_buffer,
    uint8_t            read_len,
    i2c_tx_callback_t  callback,
    void              *user_data
) {
	if (self == NULL || write_buffer == NULL || read_buffer == NULL) {
		return SL_STATUS_NULL_POINTER;
	}
	if (write_len == 0 || read_len == 0) {
		return SL_STATUS_INVALID_PARAMETER;
	}

	return _i2c_schd_transfer(
	    self,
	    address,
	    write_buffer,
	    write_len,
	    read_buffer,
	    read_len,
	    callback,
	    user_data
	);
}

sl_status_t i2c_schd_receive(
    i2c_schd_handle_t *self,
    uint8_t            address,
    uint8_t           *buffer,
    uint8_t            len,
    i2c_tx_callback_t  callback,
    void              *user_data
) {
	if (self == NULL || buffer == NULL) {
		return SL_STATUS_NULL_POINTER;
	}
	if (len == 0) {
		return SL_STATUS_INVALID_PARAMETER;
	}

	return _i2c_schd_transfer(
	    self,
	    address,
	    NULL,
	    0,
	    buffer,
	    len,
	    callback,
	    user_data
	);
}

sl_status_t i2c_schd_send(
    i2c_schd_handle_t *self,
    uint8_t            address,
    const uint8_t     *buffer,
    uint8_t            len,
    i2c_tx_callback_t  callback,
    void              *user_data
) {
	if (self == NULL || buffer == NULL) {
		return SL_STATUS_NULL_POINTER;
	}
	if (len == 0) {
		return SL_STATUS_INVALID_PARAMETER;
	}

	return _i2c_schd_transfer(
	    self,
	    address,
	    buffer,
	    len,
	    NULL,
	    0,
	    callback,
	    user_data
	);
}

void _i2c_schd_complete_tx(
    i2c_schd_handle_t *self, i2c_tx_handle_t *tx, i2c_tx_status_t status
) {
	i2c_tx_callback_t callback;
	void             *user_data;
	CORE_ATOMIC_SECTION({
		callback         = tx->callback;
		user_data        = tx->user_data;
		self->current_tx = NULL;
		self->head       = (self->head + 1) & I2C_SCHEDULER_QUEUE_MASK;
	});
	if (callback == NULL) {
		return;
	}

	callback(status, user_data);
}

void _i2c_schd_on_timeout(
    sl_sleeptimer_timer_handle_t *timer, void *user_data
) {
	(void)timer;
	i2c_schd_handle_t *self = user_data;
	i2c_tx_handle_t   *tx;
	CORE_ATOMIC_SECTION({
		tx               = (i2c_tx_handle_t *)self->current_tx;
		self->current_tx = NULL;
	});
	if (tx == NULL) {
		return;
	}
	sl_power_manager_remove_em_requirement(SL_POWER_MANAGER_EM1);

	self->stuck_flag = true;
	_i2c_schd_complete_tx(self, tx, I2C_TX_TIMEOUT);
};

sl_status_t
_i2c_schd_on_i2c_complete(sl_i2c_handle_t *i2c_bus, void *user_data) {
	(void)i2c_bus;
	i2c_schd_handle_t *self = user_data;
	i2c_tx_handle_t   *tx;
	CORE_ATOMIC_SECTION({
		tx               = (i2c_tx_handle_t *)self->current_tx;
		self->current_tx = NULL;
	});
	sl_sleeptimer_stop_timer(&self->timer);
	if (tx == NULL) {
		return SL_STATUS_OK;
	}
	sl_power_manager_remove_em_requirement(SL_POWER_MANAGER_EM1);

	_i2c_schd_complete_tx(self, tx, I2C_TX_OK);
	return SL_STATUS_OK;
}

sl_status_t i2c_tx_status_map(i2c_tx_status_t status) {
	switch (status) {
	case I2C_TX_OK:
		return SL_STATUS_OK;
	case I2C_TX_PARAMETER_ERROR:
		return SL_STATUS_INVALID_PARAMETER;
	case I2C_TX_FOLLOWER_ACK_ERROR:
		return SL_STATUS_NOT_FOUND;
	case I2C_TX_BUS_ERROR:
		return SL_STATUS_TRANSMIT;
	case I2C_TX_TIMEOUT:
		return SL_STATUS_TIMEOUT;
	default:
		return SL_STATUS_FAIL;
	}
}

sl_status_t _i2c_schd_on_i2c_event(
    sl_i2c_handle_t *i2c_bus, sl_i2c_event_t e, void *user_data
) {
	if (e == SL_I2C_EVENT_IN_PROGRESS || e == SL_I2C_EVENT_COMPLETED ||
	    e == SL_I2C_EVENT_IDLE) {
		return SL_STATUS_OK;
	}
	(void)i2c_bus;
	i2c_schd_handle_t *self = user_data;
	i2c_tx_handle_t   *tx;
	CORE_ATOMIC_SECTION({
		tx               = (i2c_tx_handle_t *)self->current_tx;
		self->current_tx = NULL;
	});
	sl_sleeptimer_stop_timer(&self->timer);

	if (tx == NULL) {
		return SL_STATUS_OK;
	}
	sl_power_manager_remove_em_requirement(SL_POWER_MANAGER_EM1);

	_i2c_schd_complete_tx(
	    self,
	    tx,
	    e == SL_I2C_EVENT_ADDR_NACK ? I2C_TX_FOLLOWER_ACK_ERROR
	                                : I2C_TX_BUS_ERROR
	);
	return SL_STATUS_OK;
}

void _i2c_schd_start_tx(i2c_schd_handle_t *self, i2c_tx_handle_t *tx) {
	if (tx->write_buffer == NULL && tx->read_buffer == NULL) {
		_i2c_schd_complete_tx(self, tx, I2C_TX_PARAMETER_ERROR);
		return;
	}

	sl_status_t status = sl_sleeptimer_start_timer_ms(
	    &self->timer,
	    tx->read_len + tx->write_len + 2,
	    _i2c_schd_on_timeout,
	    self,
	    0,
	    0
	);
	if (status != SL_STATUS_OK) {
		_i2c_schd_complete_tx(self, tx, I2C_TX_START_ERROR);
		return;
	}
	sl_power_manager_add_em_requirement(SL_POWER_MANAGER_EM1);
	if (tx->write_buffer == NULL) {
		status = sl_i2c_leader_receive_non_blocking(
		    self->i2c_bus,
		    self->current_tx->address,
		    self->current_tx->read_buffer,
		    self->current_tx->read_len,
		    self
		);
	} else if (tx->read_buffer == NULL) {
		status = sl_i2c_leader_send_non_blocking(
		    self->i2c_bus,
		    self->current_tx->address,
		    self->current_tx->write_buffer,
		    self->current_tx->write_len,
		    self
		);
	} else {
		status = sl_i2c_leader_transfer_non_blocking(
		    self->i2c_bus,
		    self->current_tx->address,
		    self->current_tx->write_buffer,
		    self->current_tx->write_len,
		    self->current_tx->read_buffer,
		    self->current_tx->read_len,
		    self
		);
	}

	if (status != SL_STATUS_OK) {
		sl_sleeptimer_stop_timer(&self->timer);
		sl_power_manager_remove_em_requirement(SL_POWER_MANAGER_EM1);
		_i2c_schd_complete_tx(self, tx, I2C_TX_START_ERROR);
	}
}

void _i2c_schd_reset_bus(i2c_schd_handle_t *self) {
	sl_i2c_deinit(self->i2c_bus);
	sl_i2c_init(self->i2c_bus, &self->init_params);
}

void i2c_schd_process_action(i2c_schd_handle_t *self) {
	CORE_DECLARE_IRQ_STATE;
	bool stuck;
	CORE_ENTER_ATOMIC();
	stuck            = self->stuck_flag;
	self->stuck_flag = false;
	CORE_EXIT_ATOMIC();

	if (stuck == true) {
		_i2c_schd_reset_bus(self);
	}

	CORE_ENTER_ATOMIC();
	if (self->current_tx != NULL) {
		CORE_EXIT_ATOMIC();
		return;
	}
	if (_i2c_sched_queue_empty(self)) {
		CORE_EXIT_ATOMIC();
		return;
	}
	i2c_tx_handle_t *tx = &self->queue[self->head];
	self->current_tx    = tx;
	CORE_EXIT_ATOMIC();
	_i2c_schd_start_tx(self, tx);
}

sl_status_t i2c_schd_init(i2c_schd_handle_t *self, sl_i2c_handle_t *i2c_bus) {
	if (self == NULL || i2c_bus == NULL) {
		return SL_STATUS_NULL_POINTER;
	}
	self->i2c_bus                    = i2c_bus;
	self->init_params.frequency_mode = self->i2c_bus->frequency_mode;
	self->init_params.i2c_peripheral = self->i2c_bus->i2c_peripheral;
	self->init_params.operating_mode = self->i2c_bus->operating_mode;
	self->init_params.scl_gpio       = self->i2c_bus->scl_gpio;
	self->init_params.sda_gpio       = self->i2c_bus->sda_gpio;
	self->current_tx                 = NULL;
	self->name                       = NULL;
	self->head                       = 0;
	self->tail                       = 0;
	self->stuck_flag                 = false;

	sl_status_t status = sl_i2c_set_transfer_complete_callback(
	    self->i2c_bus,
	    &_i2c_schd_on_i2c_complete
	);
	if (status != SL_STATUS_OK) {
		return status;
	}
	status = sl_i2c_set_event_callback(self->i2c_bus, &_i2c_schd_on_i2c_event);
	if (status != SL_STATUS_OK) {
		sl_i2c_set_transfer_complete_callback(self->i2c_bus, NULL);
		return status;
	}
	return SL_STATUS_OK;
}

uint8_t _i2c_get_index(sl_peripheral_t peripheral) {
	switch (peripheral->base) {
#ifdef I2C0_BASE
	case I2C0_BASE:
		return 0;
#endif // I2C0_BASE
#ifdef I2C1_BASE
	case I2C1_BASE:
		return 1;
#endif // I2C1_BASE
#ifdef I2C2_BASE
	case I2C2_BASE:
		return 2;
#endif // I2C2_BASE
#ifdef I2C3_BASE
	case I2C3_BASE:
		return 3;
#endif // I2C3_BASE
	default:
		return I2C_COUNT;
	}
}

#define ARRAY_SIZE(array) (sizeof(array) / sizeof((array)[0]))

const char *_i2c_schd_get_instance_name(i2c_schd_handle_t *self) {
	if (self->i2c_bus == NULL || self->i2c_bus->i2c_peripheral == NULL) {

		return "I2C<NULL>";
	}
	static const char *names[I2C_COUNT] = {
#ifdef I2C0_BASE
	    "I2C0",
#endif // I2C0_BASE
#ifdef I2C1_BASE
	    "I2C1",
#endif // I2C1_BASE
#ifdef I2C2_BASE
	    "I2C2",
#endif // I2C2_BASE
#ifdef I2C3_BASE
	    "I2C3",
#endif // I2C3_BASE
	};
	uint8_t index = _i2c_get_index(self->i2c_bus->i2c_peripheral);
	if (index >= I2C_COUNT) {
		return "I2C<Unknown>";
	}
	return names[index];
}

const char *i2c_schd_get_instance_name(i2c_schd_handle_t *self) {
	if (self == NULL) {
		return "I2C<NULL>";
	}
	if (self->name == NULL) {
		self->name = _i2c_schd_get_instance_name(self);
	}
	return self->name;
}
