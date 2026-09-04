package main

import "time"

type EnvironmentalState struct {
	Timestamp        time.Time `json:"timestamp"`
	Temperature_C    float64   `json:"temperature_C"`
	Humidity_percent float64   `json:"humidity_percent"`
	Pressure_hPa     float64   `json:"pressure_hPa"`
	CO2_PPM          float64   `json:"co2_ppm"`
}

type EnvironmentalDevice struct {
	Location           SensorLocation     `json:"location"`
	Address            string             `json:"address"`
	LastSeen           time.Time          `json:"last_seen"`
	AdvertismentPeriod time.Duration      `json:"advertisment_period"`
	Battery            int                `json:"battery"`
	MemoryUsage        float64            `json:"memory_usage"`
	Current            EnvironmentalState `json:"current_state"`
}
