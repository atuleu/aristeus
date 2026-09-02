#include "app_es.h"

#include "app_log.h"

#include "batt_monitor.h"
#include "drivers/lps22hh.h"
#include "drivers/sht4x.h"
#include "drivers/stcc4.h"
#include "em_logger.h"
#include "nvm3_default.h"
#include "nvm3_generic.h"
#include "sl_core.h"
#include "sl_sleeptimer.h"
#include "sl_status.h"
#include "types.h"
#include "utils/jitter.h"
#include "utils/status.h"
#include <stdint.h>

#define SL_STATUS_NO_STATUS 0xFFFF

#define NVM3_PRESSURE_OFFSET_KEY (NVM3_KEY_MIN + 0x00011)

typedef struct app_es_handle {
	sl_sleeptimer_timer_handle_t sensor_timer;
	sht4x_handle_t               sht4x_sensor;
	lps22hh_handle_t             lps22hh_sensor;
	stcc4_handle_t               stcc4_sensor;

	volatile data_point_t new_data_point;
	data_point_t          current_data_point;
	volatile sl_status_t  sht4x_readout_status;
	volatile sl_status_t  lps22hh_readout_status;
	volatile sl_status_t  stcc4_readout_status;
	volatile bool         sht4x_done, lps22hh_done;

	sl_sleeptimer_timestamp_t time_offset;

	app_es_readout_callback_t callback;
	pressure_t                pressure_offset;
	uint32_t                  readout_period_ms;
	volatile bool             perform_reset;
} app_es_handle_t;

static app_es_handle_t self = {
    .current_data_point =
        {
            .date        = UINT32_MAX,
            .temperature = GATT_TEMPERATURE_NAN,
            .humidity    = GATT_HUMIDITY_NAN,
            .pressure    = GATT_PRESSURE_NAN,
            .co2         = GATT_CO2_NAN,
        },
    .new_data_point =
        {
            .date        = UINT32_MAX,
            .temperature = GATT_TEMPERATURE_NAN,
            .humidity    = GATT_HUMIDITY_NAN,
            .pressure    = GATT_PRESSURE_NAN,
            .co2         = GATT_CO2_NAN,
        },

    .lps22hh_readout_status = SL_STATUS_NO_STATUS,
    .sht4x_readout_status   = SL_STATUS_NO_STATUS,
    .stcc4_readout_status   = SL_STATUS_NO_STATUS,
    .lps22hh_done           = false,
    .sht4x_done             = false,
    .time_offset            = 0,
    .pressure_offset        = 0,
};

void _app_es_on_lps22hh_readout(
    sl_status_t status, pressure_t pressure, void *user_data
) {
	(void)user_data;
	CORE_ATOMIC_SECTION({
		self.new_data_point.pressure = pressure + self.pressure_offset;
		self.lps22hh_readout_status  = status;
	});
}

void _app_es_on_sht4x_readout(
    sl_status_t   status,
    temperature_t temperature,
    humidity_t    humidity,
    void         *user_data
) {
	(void)user_data;
	CORE_ATOMIC_SECTION({
		self.new_data_point.temperature = temperature;
		self.new_data_point.humidity    = humidity;
		self.sht4x_readout_status       = status;
	});
}

void _app_es_on_stcc4_readout(
    sl_status_t status, co2_concentration_t co2, void *user_data
) {
	(void)user_data;
	CORE_ATOMIC_SECTION({
		self.new_data_point.co2   = co2;
		self.stcc4_readout_status = status;
	});
}

void _app_es_on_sensor_timer_timeout(
    sl_sleeptimer_timer_handle_t *timer, void *user_data
) {
	(void)timer;
	(void)user_data;

	em_logger_print();
	CORE_DECLARE_IRQ_STATE;
	CORE_ENTER_ATOMIC();
	self.new_data_point.date = sl_sleeptimer_get_time() + self.time_offset;
	self.new_data_point.temperature = GATT_TEMPERATURE_NAN;
	self.new_data_point.humidity    = GATT_HUMIDITY_NAN;
	self.new_data_point.pressure    = GATT_PRESSURE_NAN;
	self.new_data_point.co2         = GATT_CO2_NAN;
	self.sht4x_done                 = false;
	self.lps22hh_done               = false;
	CORE_EXIT_ATOMIC();

	batt_monitor_preempt_open();

	app_log_debug("[app_es] starting readout." APP_LOG_NL);

	sl_status_t s = sht4x_read_data(
	    &self.sht4x_sensor,
	    SHT4X_MEASURE_HIGH_P,
	    &_app_es_on_sht4x_readout,
	    NULL
	);
	if (s != SL_STATUS_OK) {
		app_log_error(
		    "[app_es] could not start SHT4X reading: %s." APP_LOG_NL,
		    sl_status_get_string(s)
		);
		CORE_ATOMIC_SECTION({
			self.sht4x_readout_status = SL_STATUS_NOT_AVAILABLE;
		});
	}

	s = lps22hh_oneshot(
	    &self.lps22hh_sensor,
	    &_app_es_on_lps22hh_readout,
	    NULL
	);
	if (s != SL_STATUS_OK) {
		app_log_error(
		    "[app_es] could not start LPS22HH reading: %s." APP_LOG_NL,
		    sl_status_get_string(s)
		);
		CORE_ATOMIC_SECTION({
			self.lps22hh_readout_status = SL_STATUS_NOT_AVAILABLE;
		});
	}
};

sl_status_t app_es_init(const app_es_config_t *config) {
	if (config->callback == NULL || config->i2c_bus == NULL) {
		return SL_STATUS_NULL_POINTER;
	}
	self.callback      = config->callback;
	self.perform_reset = false;
	sl_status_t status;
	status = sht4x_init(&self.sht4x_sensor, config->i2c_bus, SHT4X_BASE_ADDR);
	if (status != SL_STATUS_OK) {
		app_log_error(
		    "[app_es] no loop started: SHT4x init failed: %s." APP_LOG_NL,
		    sl_status_get_string(status)
		);
		return status;
	}

	lps22hh_config_t config_lps22hh = {
	    .i2c_bus       = config->i2c_bus,
	    .addrLSBSet    = false,
	    .interrupt_pin = config->data_ready_pin,
	};

	status = lps22hh_init(&self.lps22hh_sensor, &config_lps22hh);
	if (status != SL_STATUS_OK) {
		app_log_error(
		    "[app_es] no loop started: LPS22HH init failed: %s." APP_LOG_NL,
		    sl_status_get_string(status)
		);
		return status;
	}

	stcc4_init_args_t args = {
	    .i2c_bus         = config->i2c_bus,
	    .address_pin_set = false
	};
	status = stcc4_init(&self.stcc4_sensor, &args);
	if (status != SL_STATUS_OK) {
		app_log_error(
		    "[app_es] no loop started: STCC4 init failed: %s." APP_LOG_NL,
		    sl_status_get_string(status)
		);
		return status;
	}

	status = nvm3_readData(
	    nvm3_defaultHandle,
	    NVM3_PRESSURE_OFFSET_KEY,
	    &self.pressure_offset,
	    sizeof(pressure_t)
	);

	if (status != SL_STATUS_OK) {
		app_log_warning(
		    "[app_es] could not retrieve saved pressure offset: %s." APP_LOG_NL,
		    sl_status_get_string(status)
		);
		self.pressure_offset = 0;
	}

	self.readout_period_ms = config->readout_period_ms;

	status = stcc4_perform_conditioning(
	    &self.stcc4_sensor,
	    &_app_es_on_conditioning_done,
	    NULL
	);
	if (status != SL_STATUS_OK) {
		app_log_error(
		    "[app_es] could not perform STCC4 conditioning: %s." APP_LOG_NL,
		    sl_status_get_string(status)
		);
		return _app_es_start_readout_timer();
	}
	return SL_STATUS_OK;
}

void _app_es_on_conditioning_done(sl_status_t status, void *user_data) {
	(void)user_data;

	if (status != SL_STATUS_OK) {
		app_log_error(
		    "[app_es] STCC4 conditionning could not be done: %s" APP_LOG_NL,
		    sl_status_get_string(status)
		);
	}

	_app_es_start_readout_timer();
}

sl_status_t _app_es_start_readout_timer() {

	sl_status_t status = sl_sleeptimer_start_periodic_timer_ms(
	    &self.sensor_timer,
	    self.readout_period_ms,
	    &_app_es_on_sensor_timer_timeout,
	    NULL,
	    0,
	    0
	);

	if (status != SL_STATUS_OK) {
		app_log_error(
		    "[app_es] no loop started: periodic timer failed: %s." APP_LOG_NL,
		    sl_status_get_string(status)
		);
		return status;
	}
	app_log_info("[app_es] started read loop." APP_LOG_NL);

	_app_es_on_sensor_timer_timeout(NULL, NULL);

	return status;
}

sl_status_t _app_es_start_co2_readout() {

	if (self.current_data_point.temperature == GATT_TEMPERATURE_NAN) {
		app_log_error(
		    "[app_es] could not read co2 concentration: missing "
		    "temperature." APP_LOG_NL
		);
		_app_es_schedule_batt_measurement();
		return SL_STATUS_INVALID_STATE;
	}

	if (self.current_data_point.humidity == GATT_HUMIDITY_NAN) {
		app_log_error(
		    "[app_es] could not read co2 concentration: missing "
		    "humidity." APP_LOG_NL
		);
		_app_es_schedule_batt_measurement();
		return SL_STATUS_INVALID_STATE;
	}

	if (self.current_data_point.pressure == GATT_PRESSURE_NAN) {
		app_log_error(
		    "[app_es] could not read co2 concentration: missing "
		    "barometric pressure." APP_LOG_NL
		);
		_app_es_schedule_batt_measurement();
		return SL_STATUS_INVALID_STATE;
	}

	app_log_debug("[app_es] starting STCC4 measurement." APP_LOG_NL);
	sl_status_t status = stcc4_start_read_sequence(
	    &self.stcc4_sensor,
	    self.current_data_point.temperature,
	    self.current_data_point.humidity,
	    self.current_data_point.pressure,
	    &_app_es_on_stcc4_readout,
	    NULL
	);

	if (status != SL_STATUS_OK) {
		app_log_error(
		    "[app_es] could not start c02 concentration measurement: "
		    "%s." APP_LOG_NL,
		    sl_status_get_string(status)
		);
	}
	_app_es_schedule_batt_measurement();
	return status;
}

void _app_es_schedule_batt_measurement() {
	batt_monitor_start_loaded_measurement(
	    sl_sleeptimer_ms_to_tick(37) + jitter()
	);
}

bool app_es_is_ok_to_sleep() {
	return self.sht4x_readout_status == SL_STATUS_NO_STATUS &&
	       self.lps22hh_readout_status == SL_STATUS_NO_STATUS &&
	       self.stcc4_readout_status == SL_STATUS_NO_STATUS;
}

void app_es_process_action(void) {
	sl_status_t sht4x_status, lps22hh_status, stcc4_status;
	CORE_DECLARE_IRQ_STATE;
	CORE_ENTER_ATOMIC();
	if (app_es_is_ok_to_sleep()) {
		CORE_EXIT_ATOMIC();
		return;
	}
	sht4x_status                = self.sht4x_readout_status;
	lps22hh_status              = self.lps22hh_readout_status;
	stcc4_status                = self.stcc4_readout_status;
	self.sht4x_readout_status   = SL_STATUS_NO_STATUS;
	self.lps22hh_readout_status = SL_STATUS_NO_STATUS;
	self.stcc4_readout_status   = SL_STATUS_NO_STATUS;
	CORE_EXIT_ATOMIC();

	if (sht4x_status != SL_STATUS_NO_STATUS) {
		_app_es_process_sht4x(sht4x_status);
	}

	if (lps22hh_status != SL_STATUS_NO_STATUS) {
		_app_es_process_lps22hh(lps22hh_status);
	}

	if (self.lps22hh_done == true && self.sht4x_done == true) {
		self.sht4x_done    = false;
		self.lps22hh_done  = false;
		sl_status_t status = _app_es_start_co2_readout();
		if (status != SL_STATUS_OK) {
			_app_es_complete_readout(SL_STATUS_OK);
		}
	}

	if (stcc4_status != SL_STATUS_NO_STATUS) {
		_app_es_process_stcc4(stcc4_status);
	}
}

void _app_es_process_sht4x(sl_status_t status) {
	self.current_data_point.date = self.new_data_point.date;
	self.sht4x_done              = true;
	if (status != SL_STATUS_OK) {
		app_log_error(
		    "[app_es] SHT4x measurement error: %s." APP_LOG_NL,
		    sl_status_get_string(status)
		);
		return;
	}
	self.current_data_point.temperature = self.new_data_point.temperature;
	self.current_data_point.humidity    = self.new_data_point.humidity;

	app_log_info(
	    "[app_es] temperature: %d.%02d°C humidity: %d.%d%%." APP_LOG_NL,
	    self.new_data_point.temperature / 100,
	    self.new_data_point.temperature % 100,
	    self.new_data_point.humidity / 10,
	    self.new_data_point.humidity % 10
	);
}

void _app_es_process_lps22hh(sl_status_t status) {
	self.current_data_point.date = self.new_data_point.date;
	self.lps22hh_done            = true;
	if (status != SL_STATUS_OK) {
		app_log_error(
		    "[app_es] LPS22HH measurement error: %s." APP_LOG_NL,
		    sl_status_get_string(status)
		);
		return;
	}
	self.current_data_point.pressure = self.new_data_point.pressure;

	app_log_info(
	    "[app_es] pressure: %ld.%03ldhPa." APP_LOG_NL,
	    self.new_data_point.pressure / 1000,
	    self.new_data_point.pressure % 1000
	);
}

void _app_es_process_stcc4(sl_status_t status) {
	self.current_data_point.date = self.new_data_point.date;
	if (status != SL_STATUS_OK) {
		app_log_error(
		    "[app_es] STCC4 measurement error: %s." APP_LOG_NL,
		    sl_status_get_string(status)
		);
	} else {
		self.current_data_point.co2 = self.new_data_point.co2;
		app_log_info(
		    "[app_es] got CO2 concentration %dppm." APP_LOG_NL,
		    self.new_data_point.co2
		);
	}
	_app_es_complete_readout(SL_STATUS_OK);
}

sl_sleeptimer_timestamp_t app_es_get_current_unix_time() {
	sl_sleeptimer_timestamp_t offset;
	CORE_ATOMIC_SECTION({ offset = self.time_offset; });
	return sl_sleeptimer_get_time() + offset;
}

void app_es_set_current_unix_time(sl_sleeptimer_timestamp_t now) {
	uint32_t old_offset;
	CORE_ATOMIC_SECTION({
		old_offset       = self.time_offset;
		self.time_offset = now - sl_sleeptimer_get_time();
	});
	uint32_t diff = self.time_offset - old_offset;
	if (self.new_data_point.date != UINT32_MAX) {
		self.new_data_point.date += diff;
	}
	self.current_data_point.date += diff;
}

const data_point_t *app_es_current_data() {
	return &self.current_data_point;
}

void _app_es_complete_readout(sl_status_t status) {
	data_point_t new_data_point;
	CORE_ATOMIC_SECTION({ new_data_point = self.new_data_point; });
	self.callback(status, &new_data_point);
	batt_monitor_enable_open();

	bool perform_reset;
	CORE_ATOMIC_SECTION({
		perform_reset      = self.perform_reset;
		self.perform_reset = false;
	});

	if (perform_reset == false) {

		return;
	}
	status = stcc4_factory_reset(
	    &self.stcc4_sensor,
	    &_app_es_on_factory_reset,
	    NULL
	);
	if (status != SL_STATUS_OK) {
		app_log_error(
		    "[app_es] could not STCC4 perform factory reset: %s" APP_LOG_NL,
		    sl_status_get_string(status)
		);
		CORE_ATOMIC_SECTION({ self.perform_reset = true; });
	}
}

void _app_es_on_factory_reset(sl_status_t status, void *user_data) {
	(void)user_data;
	stcc4_enter_sleep_mode(&self.stcc4_sensor);
	if (status != SL_STATUS_OK) {
		app_log_error(
		    "[app_es] could not STCC4 perform factory reset: %s" APP_LOG_NL,
		    sl_status_get_string(status)
		);
		CORE_ATOMIC_SECTION({ self.perform_reset = true; });
		return;
	}
	if (batt_monitor_get_current_level() != BATTERY_NAN) {
		// we are running on battery, meaning we cannot make continuous
		// measurement
		app_log_warning(
		    "[app_es] factory reset for STCC4, needs 6 hours to "
		    "re-calibrate on battery." APP_LOG_NL
		);
		return;
	}

	status = sl_sleeptimer_stop_timer(&self.sensor_timer);
	if (status != SL_STATUS_OK) {
		app_log_error(
		    "[app_es]: could not stop readout loop: %s" APP_LOG_NL,
		    sl_status_get_string(status)
		);
		return;
	}

	status = sl_sleeptimer_start_timer_ms(
	    &self.sensor_timer,
	    3660 * 1000,
	    &_app_es_on_stcc4_init_timeout,
	    NULL,
	    0,
	    0
	);
	if (status != SL_STATUS_OK) {
		app_log_error(
		    "[app_es]: could not set up timer for STCC4 initialization: "
		    "%s" APP_LOG_NL,
		    sl_status_get_string(status)
		);
		_app_es_start_readout_timer();
		return;
	}
	status = stcc4_start_continuous_measurement(
	    &self.stcc4_sensor,
	    &_app_es_on_stcc4_start_continuous,
	    NULL
	);
	if (status != SL_STATUS_OK) {
		app_log_error(
		    "[app_es] could not start continuous measurement: %s" APP_LOG_NL,
		    sl_status_get_string(status)
		);
		sl_sleeptimer_stop_timer(&self.sensor_timer);
		_app_es_start_readout_timer();
	}
	app_log_warning(
	    "[app_es] measurement disabled for 1h for STCC4 "
	    "re-calibration" APP_LOG_NL
	);
}

void _app_es_on_stcc4_start_continuous(sl_status_t status, void *user_data) {
	(void)user_data;
	if (status != SL_STATUS_OK) {
		app_log_warning(
		    "[app_es] could not start continuous measurement for STCC4: "
		    "%s" APP_LOG_NL,
		    sl_status_get_string(status)
		);
		_app_es_start_readout_timer();
	}
}

void _app_es_on_stcc4_init_timeout(
    sl_sleeptimer_timer_handle_t *timer, void *user_data
) {
	(void)timer;
	(void)user_data;
	sl_status_t status = stcc4_stop_continous_measurement(
	    &self.stcc4_sensor,
	    &_app_es_on_stcc4_stop_continuous,
	    NULL
	);
	if (status == SL_STATUS_OK) {
		return;
	}
	app_log_error(
	    "[app_es] could not stop STCC4 continuous measurement: "
	    "%s" APP_LOG_NL "[app_es] retrying in 500ms" APP_LOG_NL,
	    sl_status_get_string(status)
	);
	sl_sleeptimer_start_timer_ms(
	    &self.sensor_timer,
	    500,
	    &_app_es_on_sensor_timer_timeout,
	    NULL,
	    0,
	    0
	);
}

void _app_es_on_stcc4_stop_continuous(sl_status_t status, void *user_data) {
	(void)user_data;
	if (status == SL_STATUS_OK) {
		_app_es_start_readout_timer();
		return;
	}

	app_log_error(
	    "[app_es] could not stop STCC4 continuous measurement: %s" APP_LOG_NL,
	    sl_status_get_string(status)
	);

	sl_sleeptimer_start_timer_ms(
	    &self.sensor_timer,
	    500,
	    &_app_es_on_sensor_timer_timeout,
	    NULL,
	    0,
	    0
	);
}

pressure_t app_es_current_pressure() {
	if (self.current_data_point.pressure == GATT_PRESSURE_NAN) {
		return GATT_PRESSURE_NAN;
	}
	return self.current_data_point.pressure;
}

sl_status_t app_es_tare_pressure(pressure_t pressure) {
	if (self.current_data_point.pressure == GATT_PRESSURE_NAN) {
		return SL_STATUS_INVALID_STATE;
	}
	pressure_t new_offset;

	CORE_ATOMIC_SECTION({
		new_offset =
		    pressure - self.current_data_point.pressure + self.pressure_offset;
	});

	sl_status_t status = nvm3_writeData(
	    nvm3_defaultHandle,
	    NVM3_PRESSURE_OFFSET_KEY,
	    &new_offset,
	    sizeof(pressure_t)
	);
	if (status != SL_STATUS_OK) {
		app_log_error(
		    "[app_es] could not save new pressure offset: %s." APP_LOG_NL,
		    sl_status_get_string(status)
		);
		return SL_STATUS_FAIL;
	}

	CORE_ATOMIC_SECTION({
		self.pressure_offset             = new_offset;
		self.current_data_point.pressure = pressure;
		self.perform_reset               = true;
	});

	app_log_info(
	    "[app_es] new pressure offset %ld.%03ldhPa current pressure "
	    "%ld.%03ldhPa." APP_LOG_NL,
	    (int32_t)pressure / 1000,
	    (int32_t)pressure % 1000,
	    self.current_data_point.pressure / 1000,
	    self.current_data_point.pressure % 1000
	);

	return SL_STATUS_OK;
}
