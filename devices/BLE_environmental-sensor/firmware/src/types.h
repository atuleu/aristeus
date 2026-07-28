#pragma once

#include "sl_sleeptimer.h"
#include <stdint.h>

/// Temperature value in 0.01°C increment, as per GATT specification 0x8000 is
/// NaN
typedef int16_t temperature_t;

#define GATT_TEMPERATURE_NAN ((temperature_t)0x8000)
/// Humidity value in 0.1% increment, as per GATT specification. 0xFFFF is NaN
typedef uint16_t humidity_t;

#define GATT_HUMIDITY_NAN ((humidity_t)0xFFFF)
/// Pressure in 0.1 Pa increment, following GATT specification, 0xFFFFFFFF is
/// NaN
#define GATT_PRESSURE_NAN ((pressure_t)0xFFFFFFFF)
typedef uint32_t pressure_t;
/// CO2 Concentration in 1PPM, following GATT specification. 0xFFFE: saturation,
/// 0xFFFF NaN
typedef uint16_t co2_concentration_t;

#define GATT_CO2_NAN ((co2_concentration_t)0xFFFF)
#define GATT_CO2_MAX ((co2_concentration_t)0xFFFE)

typedef struct __attribute__((packed)) data_point {
	sl_sleeptimer_timestamp_t date;
	temperature_t             temperature;
	humidity_t                humidity;
	pressure_t                pressure;
	co2_concentration_t       c02;
} data_point_t;

static_assert(sizeof(data_point_t) == 14, "Data point size mismatch");
