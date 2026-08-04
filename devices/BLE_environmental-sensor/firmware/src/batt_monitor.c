#include "batt_monitor.h"
#include "app_log.h"
#include "em_iadc.h"
#include "sl_clock_manager.h"
#include "sl_core.h"
#include "sl_device_clock.h"
#include "sl_interrupt_manager.h"
#include "sl_power_manager_config.h"
#include "sl_status.h"
#include <stdint.h>

#if SL_POWER_MANAGER_RAMP_DVDD_EN == 1
#error                                                                         \
    "SL_POWER_MANAGER_RAMP_DVDD_EN should be disabled for the battery monitor. We do not use EM4 sleep"
#endif

#define ADC_CLK_FREQ 1000000

typedef struct batt_monitor {
	battery_level_t        current_level;
	volatile IADC_Result_t result;
	volatile bool          new_result;
	uint32_t               vref;
	uint16_t               voltage;
} batt_monitor_t;

static batt_monitor_t self = {
    .current_level = BATTERY_NAN,
    .result        = {.data = 4095, .id = 0},
    .new_result    = false,
};

sl_status_t batt_monitor_init() {
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
	CORE_ATOMIC_SECTION({
		self.result     = IADC_pullSingleFifoResult(IADC0);
		self.new_result = true;
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
	uint32_t voltage_mv;
	uint8_t  capacity;
} batt_model_entry_t;

battery_level_t batt_monitor_get_current_level() {
	bool          new_result;
	IADC_Result_t result;
	CORE_ATOMIC_SECTION({
		new_result = self.new_result;
		if (new_result == true) {
			result = self.result;
		}
		self.new_result = false;
	});

	if (new_result == false) {
		return self.current_level;
	}

	uint32_t avdd_mv = result.data * self.vref / 4095;
	app_log_info("[batt_monitor] measured AVDD=%lumV." APP_LOG_NL, avdd_mv);

#define CR2032_MODEL_ENTRY_SIZE 8
	static batt_model_entry_t cr2032_model[CR2032_MODEL_ENTRY_SIZE] = {
	    {.voltage_mv = 3000, .capacity = 100},
	    {.voltage_mv = 2900, .capacity = 80},
	    {.voltage_mv = 2800, .capacity = 60},
	    {.voltage_mv = 2700, .capacity = 40},
	    {.voltage_mv = 2600, .capacity = 30},
	    {.voltage_mv = 2500, .capacity = 20},
	    {.voltage_mv = 2400, .capacity = 10},
	    {.voltage_mv = 2000, .capacity = 0},
	};

	if (avdd_mv > 3100) {
		app_log_warning(
		    "[batt_monitor] likely running on external power supply." APP_LOG_NL
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

sl_status_t batt_monitor_start_measurement() {
	app_log_debug("[batt_monitor] measurement started." APP_LOG_NL);
	sl_status_t status = sl_clock_manager_enable_bus_clock(SL_BUS_CLOCK_IADC0);
	if (status != SL_STATUS_OK) {
		return status;
	}
	IADC0->EN_SET = IADC_EN_EN;
	IADC_command(IADC0, iadcCmdStartSingle);

	return SL_STATUS_OK;
}
