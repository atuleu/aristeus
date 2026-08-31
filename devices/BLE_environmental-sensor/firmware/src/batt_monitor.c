#include "batt_monitor.h"
#include "app_log.h"
#include "em_iadc.h"
#include "sl_clock_manager.h"
#include "sl_core.h"
#include "sl_device_clock.h"
#include "sl_interrupt_manager.h"
#include "sl_power_manager.h"
#include "sl_power_manager_config.h"
#include "sl_sleeptimer.h"
#include "sl_status.h"
#include "utils/status.h"
#include <stdint.h>
#include <sys/reent.h>

#if SL_POWER_MANAGER_RAMP_DVDD_EN == 1
#error                                                                         \
    "SL_POWER_MANAGER_RAMP_DVDD_EN should be disabled for the battery monitor. We do not use EM4 sleep"
#endif

#define ADC_CLK_FREQ 1000000

#define OPEN_VOLTAGE_MV_SIZE 32
static_assert(
    (OPEN_VOLTAGE_MV_SIZE & (OPEN_VOLTAGE_MV_SIZE - 1)) == 0,
    "OPEN_VOLTAGE_MV_SIZE must be a power of 2"
);
static_assert(OPEN_VOLTAGE_MV_SIZE <= 256, "OPEN_VOLTAGE_SIZE is too large");
#define LOADED_VOLTAGE_MV_SIZE 32
static_assert(
    (LOADED_VOLTAGE_MV_SIZE & (LOADED_VOLTAGE_MV_SIZE - 1)) == 0,
    "LOADED_VOLTAGE_MV_SIZE must be a power of 2"
);

static_assert(LOADED_VOLTAGE_MV_SIZE <= 256, "OPEN_VOLTAGE_SIZE is too large");

#define VA_SNAME(Name) voltage_averager_##Name##_t
#define VA_SIZE(Name)  Name##_VOLTAGE_MV_SIZE
#define VA_MASK(Name)  (Name##_VOLTAGE_MV_SIZE - 1)

#define IMPLEMENT_VOLTAGE_AVERAGER(Name)                                       \
	typedef struct voltage_averager_##Name {                                   \
		volatile uint16_t voltage_mV[VA_SIZE(Name)];                           \
		volatile uint32_t size;                                                \
	} VA_SNAME(Name);                                                          \
	void Name##_voltage_init(VA_SNAME(Name) * self) {                          \
		for (uint8_t i = 0; i < VA_SIZE(Name); ++i) {                          \
			self->voltage_mV[i] = 0;                                           \
		}                                                                      \
		self->size = 0;                                                        \
	}                                                                          \
	void Name##_voltage_add(VA_SNAME(Name) * self, uint16_t value) {           \
		self->voltage_mV[(self->size++) & VA_MASK(Name)] = value;              \
	}                                                                          \
	uint16_t Name##_voltage_get(VA_SNAME(Name) * self) {                       \
		uint8_t i;                                                             \
		uint8_t size;                                                          \
		CORE_ATOMIC_SECTION({                                                  \
			size = self->size > VA_SIZE(Name) ? VA_SIZE(Name) : self->size;    \
		});                                                                    \
		if (size == 0) {                                                       \
			return 0xFFFF;                                                     \
		}                                                                      \
		uint32_t res = 0;                                                      \
		for (i = 0; i < size; ++i) {                                           \
			CORE_ATOMIC_SECTION({ res += self->voltage_mV[i]; });              \
		}                                                                      \
		return res / size;                                                     \
	}                                                                          \
	uint16_t Name##_voltage_get_last(VA_SNAME(Name) * self) {                  \
		if (self->size == 0) {                                                 \
			return 0xFFFF;                                                     \
		}                                                                      \
		return self->voltage_mV[(self->size - 1) & VA_MASK(Name)];             \
	}

IMPLEMENT_VOLTAGE_AVERAGER(OPEN)
IMPLEMENT_VOLTAGE_AVERAGER(LOADED)

SL_ENUM(_batt_monitor_next_measurement){
    _batt_monitor_next_none = 0,
    _batt_monitor_next_open,
    _batt_monitor_next_loaded,
};

const char *
_batt_monitor_next_measurement_string(_batt_monitor_next_measurement t) {
	switch (t) {
	case _batt_monitor_next_none:
		return "<NONE>";
	case _batt_monitor_next_loaded:
		return "loaded";
	case _batt_monitor_next_open:
		return "open";
	default:
		return "UNKNOWN";
	}
}

typedef struct batt_monitor {

	sl_sleeptimer_timer_handle_t            delay_timer;
	battery_level_t                         current_level;
	volatile _batt_monitor_next_measurement next_measurement;
	volatile bool                           has_new_data;
	volatile uint8_t                        preempt_open;

	voltage_averager_LOADED_t loaded_voltage;
	voltage_averager_OPEN_t   open_voltage;

	uint32_t vref;
	uint16_t voltage;
} batt_monitor_t;

static batt_monitor_t self = {
    .current_level    = BATTERY_NAN,
    .next_measurement = _batt_monitor_next_none,
    .has_new_data     = false,
    .preempt_open     = 0,
};

sl_status_t batt_monitor_init() {
	OPEN_voltage_init(&self.open_voltage);
	LOADED_voltage_init(&self.loaded_voltage);

	sl_status_t status = sl_clock_manager_enable_bus_clock(SL_BUS_CLOCK_IADC0);
	if (status != SL_STATUS_OK) {
		return status;
	}

	IADC_Init_t init    = IADC_INIT_DEFAULT;
	init.warmup         = iadcWarmupNormal;
	init.srcClkPrescale = IADC_calcSrcClkPrescale(IADC0, ADC_CLK_FREQ, 0);

	IADC_AllConfigs_t init_all         = IADC_ALLCONFIGS_DEFAULT;
	init_all.configs[0].adcClkPrescale = IADC_calcAdcClkPrescale(
	    IADC0,
	    ADC_CLK_FREQ,
	    0,
	    iadcCfgModeNormal,
	    init.srcClkPrescale
	);
	init_all.configs[0].reference = iadcCfgReferenceInt1V2;
	init_all.configs[0].vRef = IADC_getReferenceVoltage(iadcCfgReferenceInt1V2);
	init_all.configs[0].digAvg = iadcDigitalAverage16;

	IADC_InitSingle_t init_single = IADC_INITSINGLE_DEFAULT;

	IADC_SingleInput_t init_single_input = IADC_SINGLEINPUT_DEFAULT;
	init_single_input.posInput           = iadcPosInputAvdd;
	// the Avdd as a 0.25x prescale that we need to compensate
	self.vref                            = init_all.configs[0].vRef * 4;

	IADC_init(IADC0, &init, &init_all);
	IADC_initSingle(IADC0, &init_single, &init_single_input);

	IADC_clearInt(IADC0, _IADC_IF_MASK);
	IADC_enableInt(IADC0, IADC_IEN_SINGLEDONE);
	sl_interrupt_manager_clear_irq_pending(IADC_IRQn);
	sl_interrupt_manager_enable_irq(IADC_IRQn);

	return SL_STATUS_OK;
}

void IADC_IRQHandler(void) {
	sl_power_manager_remove_em_requirement(SL_POWER_MANAGER_EM1);
	IADC_Result_t result = IADC_pullSingleFifoResult(IADC0);
	if (result.data > 4095) {
		result.data = 4095;
	}
	CORE_ATOMIC_SECTION({
		switch (self.next_measurement) {
		case _batt_monitor_next_open:
			OPEN_voltage_add(&self.open_voltage, result.data);
			self.has_new_data = true;
			break;
		case _batt_monitor_next_loaded:
			LOADED_voltage_add(&self.loaded_voltage, result.data);
			self.has_new_data = true;
			break;
		case _batt_monitor_next_none:
		default:
		}
		self.next_measurement = _batt_monitor_next_none;
	});

	IADC_clearInt(IADC0, IADC_IF_SINGLEDONE);

#if defined(IADC_STATUS_SYNCBUSY)
	while ((IADC0->STATUS & IADC_STATUS_SYNCBUSY) != 0U) {
		// wait for synchronization
	}
#endif // defined(IADC_STATUS_SYNCBUSY)

	IADC0->EN_CLR = IADC_EN_EN;

#if defined(_IADC_EN_DISABLING_MASK) && defined(IADC_EN_DISABLING)
	while ((IADC0->EN & _IADC_EN_DISABLING_MASK) == IADC_EN_DISABLING) {
	}
#endif // defined(_IADC_EN_DISABLING_MASK) && defined(IADC_EN_DISABLING)

	(void)sl_clock_manager_disable_bus_clock(SL_BUS_CLOCK_IADC0);
}

typedef struct batt_model_entry {
	uint16_t voltage_mv;
	uint8_t  capacity;
} batt_model_entry_t;

battery_level_t batt_monitor_get_current_level() {
	bool has_new_data;
	CORE_ATOMIC_SECTION({
		has_new_data      = self.has_new_data;
		self.has_new_data = false;
	});

	if (has_new_data == false) {
		return self.current_level;
	}

	uint16_t avdd_mv = batt_monitor_loaded_voltage_mV();
	app_log_info("[batt_monitor] measured AVDD=%dumV." APP_LOG_NL, avdd_mv);

#define CR2032_MODEL_ENTRY_SIZE 8
	static batt_model_entry_t cr2032_model[CR2032_MODEL_ENTRY_SIZE] = {
	    {.voltage_mv = 3200, .capacity = 100},
	    {.voltage_mv = 2975, .capacity = 99},
	    {.voltage_mv = 2900, .capacity = 98},
	    {.voltage_mv = 2850, .capacity = 95},
	    {.voltage_mv = 2800, .capacity = 80},
	    {.voltage_mv = 2675, .capacity = 40},
	    {.voltage_mv = 2525, .capacity = 20},
	    {.voltage_mv = 2200, .capacity = 0},
	};

	if (avdd_mv > 3250) {
		app_log_info(
		    "[batt_monitor] likely running on external power "
		    "supply." APP_LOG_NL
		);
		self.current_level = BATTERY_NAN;
		return BATTERY_NAN;
	}

	if (avdd_mv >= cr2032_model[0].voltage_mv) {
		self.current_level = cr2032_model[0].capacity;
	} else if (avdd_mv <=
	           cr2032_model[CR2032_MODEL_ENTRY_SIZE - 1].voltage_mv) {
		self.current_level = cr2032_model[CR2032_MODEL_ENTRY_SIZE - 1].capacity;
	} else {

		for (uint8_t i = 0; i < (CR2032_MODEL_ENTRY_SIZE - 1); ++i) {
			if (avdd_mv < cr2032_model[i + 1].voltage_mv) {
				continue;
			}
			// avdd is smaller than i and greater or equal to i+1
			self.current_level =
			    ((avdd_mv - cr2032_model[i + 1].voltage_mv) *
			     (cr2032_model[i].capacity - cr2032_model[i + 1].capacity) /
			     (cr2032_model[i].voltage_mv - cr2032_model[i + 1].voltage_mv));
			self.current_level += cr2032_model[i + 1].capacity;
			break;
		}
	}
	app_log_info(
	    "[batt monitor] estimated capacity: %d." APP_LOG_NL,
	    self.current_level
	);
	return self.current_level;
}

void _batt_monitor_on_delay_timeout(
    sl_sleeptimer_timer_handle_t *timer, void *user_data
);

sl_status_t _batt_monitor_start_next_measurement();

sl_status_t _batt_monitor_start_measurement(
    _batt_monitor_next_measurement type, uint32_t delay_ticks
) {
	if (type == _batt_monitor_next_none) {
		return SL_STATUS_INVALID_TYPE;
	}
	CORE_DECLARE_IRQ_STATE;
	CORE_ENTER_ATOMIC();
	if (self.next_measurement != _batt_monitor_next_none) {
		CORE_EXIT_ATOMIC();
		return SL_STATUS_BUSY;
	}
	self.next_measurement = type;
	CORE_EXIT_ATOMIC();

	sl_status_t status;
	if (delay_ticks == 0) {
		status = _batt_monitor_start_next_measurement();
	} else {
		status = sl_sleeptimer_start_timer(
		    &self.delay_timer,
		    delay_ticks,
		    &_batt_monitor_on_delay_timeout,
		    NULL,
		    0,
		    0
		);
	}
	if (status != SL_STATUS_OK) {
		CORE_ENTER_ATOMIC();
		self.next_measurement = _batt_monitor_next_none;
		CORE_EXIT_ATOMIC();
		if (type != _batt_monitor_next_open && status != SL_STATUS_BUSY) {
			app_log_error(
			    "[batt_monitor] could not start %s measurement: %s." APP_LOG_NL,
			    _batt_monitor_next_measurement_string(type),
			    sl_status_get_string(status)
			);
		}
	}
	return status;
}

void _batt_monitor_on_delay_timeout(
    sl_sleeptimer_timer_handle_t *timer, void *user_data
) {
	(void)timer;
	(void)user_data;
	sl_status_t status = _batt_monitor_start_next_measurement();
	if (status != SL_STATUS_OK) {
		_batt_monitor_next_measurement type;
		CORE_ATOMIC_SECTION({
			type                  = self.next_measurement;
			self.next_measurement = _batt_monitor_next_none;
		});
		if (type != _batt_monitor_next_open && status != SL_STATUS_BUSY) {
			app_log_error(
			    "[batt_monitor] could not start %s measurement: %s." APP_LOG_NL,
			    _batt_monitor_next_measurement_string(type),
			    sl_status_get_string(status)
			);
		}
	}
}

sl_status_t _batt_monitor_start_next_measurement() {
#if APP_LOG_ENABLE == 1
	_batt_monitor_next_measurement type;
#endif // APP_LOG_ENABLE == 1
	CORE_DECLARE_IRQ_STATE;
	CORE_ENTER_ATOMIC();

	if (self.next_measurement == _batt_monitor_next_none) {
		CORE_EXIT_ATOMIC();
		return SL_STATUS_INVALID_STATE;
	}
	if (self.next_measurement == _batt_monitor_next_open &&
	    self.preempt_open > 0) {
		CORE_EXIT_ATOMIC();
		return SL_STATUS_BUSY;
	}
#if APP_LOG_ENABLE == 1
	type = self.next_measurement;
#endif // APP_LOG_ENABLE == 1
	CORE_EXIT_ATOMIC();

	sl_status_t status = sl_clock_manager_enable_bus_clock(SL_BUS_CLOCK_IADC0);
	if (status != SL_STATUS_OK) {
		return status;
	}
	sl_power_manager_add_em_requirement(SL_POWER_MANAGER_EM1);

	IADC0->EN_SET = IADC_EN_EN;
	IADC_command(IADC0, iadcCmdStartSingle);

	app_log_debug(
	    "[batt_monitor] %s measurement started." APP_LOG_NL,
	    _batt_monitor_next_measurement_string(type)
	);

	return SL_STATUS_OK;
}

sl_status_t batt_monitor_start_open_measurement(uint32_t delay_ticks) {
	return _batt_monitor_start_measurement(
	    _batt_monitor_next_open,
	    delay_ticks
	);
}

sl_status_t batt_monitor_start_loaded_measurement(uint32_t delay_ticks) {
	return _batt_monitor_start_measurement(
	    _batt_monitor_next_loaded,
	    delay_ticks
	);
}

uint16_t batt_monitor_open_voltage_mV() {
	uint16_t value = OPEN_voltage_get(&self.open_voltage);
	if (value == 0xffff) {
		return 0xffff;
	}
	return (value * self.vref) / 4095;
}

uint16_t batt_monitor_loaded_voltage_mV() {
	uint16_t value = LOADED_voltage_get(&self.loaded_voltage);
	if (value == 0xffff) {
		return 0xffff;
	}
	return (value * self.vref) / 4095;
}

void batt_monitor_preempt_open() {
#if APP_LOG_ENABLE == 1
	uint8_t p;
#endif //
	CORE_ATOMIC_SECTION({
		if (self.preempt_open < 255) {
			self.preempt_open += 1;
		}
#if APP_LOG_ENABLE == 1
		p = self.preempt_open;
#endif // APP_LOG_ENABLE == 1
	});
	app_log_debug("[batt_monitor] premption increased to %d." APP_LOG_NL, p);
}

void batt_monitor_enable_open() {
#if APP_LOG_ENABLE == 1
	uint8_t p;
#endif // APP_LOG_ENABLE == 1
	CORE_ATOMIC_SECTION({
		if (self.preempt_open > 0) {
			self.preempt_open -= 1;
		}
#if APP_LOG_ENABLE == 1
		p = self.preempt_open;
#endif // APP_LOG_ENABLE == 1
	});
	app_log_debug("[batt_monitor] premption decreased to %d." APP_LOG_NL, p);
}
