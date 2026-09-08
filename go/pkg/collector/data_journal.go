package collector

import (
	"context"
	"time"
)

type EnvironmentalReading struct {
	LocationID       string
	SensorID         string
	Timestamp        time.Time
	ReceivedAt       time.Time
	Temperature_C    *float64
	Humidity_percent *float64
	Pressure_hPa     *float64
	CO2_ppm          *uint
}

type SensorLocation struct {
	LocationID  string `json:"location_id"`
	HiveID      string `json:"hive_id"`
	Description string `json:"description"`
}

type SensorAssignement struct {
	SensorID    string
	LocationID  string
	InstalledAt time.Time
	RemovedAt   *time.Time
}

type DataJournalWriter interface {
	Close() error

	SaveEnvironmentalReadings(ctx context.Context, readings []EnvironmentalReading) error
}

type DataJournalReader interface {
	Close() error

	GetEnvironmentalHistory(ctx context.Context, locationID string, start, end time.Time) ([]EnvironmentalReading, error)
}

type TopologyManager interface {
	Close() error

	SaveLocation(ctx context.Context, loc SensorLocation) error
	GetSensorLocations(ctx context.Context) ([]SensorLocation, error)
	GetLocationAssignements(ctx context.Context, locationID string) ([]SensorAssignement, error)
	GetSensorAssignements(ctx context.Context, sensorID string) ([]SensorAssignement, error)
	GetActiveAssignments(ctx context.Context) ([]SensorAssignement, error)
}

type DataJournal interface {
	DataJournalReader
	DataJournalWriter
	TopologyManager
}
