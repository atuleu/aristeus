#pragma once

#include <stdint.h>

#include "sl_enum.h"
#include "sl_sleeptimer.h"

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

typedef uint8_t battery_level_t;
#define BATTERY_NAN 0xff

SL_ENUM(placement_t){
    placement_general = 0x00,
    placement_outside = 0x01,
    placement_inside  = 0x02,
    placement_top     = 0x04,
    placement_front   = 0x08,
    placement_left    = 0x10,
};

typedef struct __attribute__((packed)) location {
	uint8_t     hive_id;
	placement_t placement;
} location_t;

static_assert(sizeof(location_t) == 2, "Invalid location size");

typedef struct __attribute__((packed)) data_point {
	sl_sleeptimer_timestamp_t date;        // 4
	temperature_t             temperature; // 6
	humidity_t                humidity;    // 8
	pressure_t                pressure;    // 12
	co2_concentration_t       co2;         // 14
} data_point_t;

static_assert(sizeof(data_point_t) == 14, "Data point size mismatch");

typedef struct __attribute__((packed)) advertisement_data {
	uint8_t         ad_type;         // 0
	uint16_t        manufacturer_id; // 2
	location_t      location;        // 4
	battery_level_t battery;         // 5
	uint8_t         memory_level;    // 6
	data_point_t    measurement;     // 20

} advertisement_data_t;

static_assert(
    sizeof(advertisement_data_t) < 25,
    "advertisement data is too large for legacy packets"
);
