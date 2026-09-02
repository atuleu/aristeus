package main

import (
	"context"
	"time"
)

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
	GetLocationAssignement(ctx context.Context, locationID string) ([]SensorAssignement, error)
	GetSensorAssignement(ctx context.Context, sensorID string) ([]SensorAssignement, error)
}

type DataJournal interface {
	DataJournalReader
	DataJournalWriter
	TopologyManager
}
