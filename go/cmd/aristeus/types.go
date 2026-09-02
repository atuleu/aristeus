package main

import "time"

type EnvironmentalReading struct {
	LocationID       string    `json:"location_id"`
	SensorID         string    `json:"sensor_id"`
	Timestamp        time.Time `json:"timestamp"`
	ReceivedAt       time.Time `json:"received_at"`
	Temperature_C    *float64  `json:"temperature_C"`
	Humidity_percent *float64  `json:"humidity_percent"`
	Pressure_hPa     *float64  `json:"pressure_hPA"`
	CO2_ppm          *uint     `json:"CO2_ppm"`
}

type ScaleReading struct {
	LocationID       string      `json:"location_id"`
	SensorID         string      `json:"sensor_id"`
	Timestamp        time.Time   `json:"timestamp"`
	ReceivedAt       time.Time   `json:"received_at"`
	Temperature_C    *float64    `json:"temperature_C"`
	Humidity_percent *float64    `json:"humidity_percent"`
	TotalWeight_kg   *float64    `json:"total_weight_kg"`
	CellWeight_kg    *[4]float64 `json:"cell_weight_kg"`
}

type SensorLocation struct {
	LocationID  string `json:"location_id"`
	HiveID      string `json:"hive_id"`
	Description string `json:"description"`
}

type SensorAssignement struct {
	SensorID    string     `json:"sensor_id"`
	LocationID  string     `json:"location_id"`
	InstalledAt time.Time  `json:"installed_at"`
	RemovedAt   *time.Time `json:"removed_at"`
}
