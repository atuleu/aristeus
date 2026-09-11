package collector

import (
	"context"
	"math"
	"time"

	"github.com/atuleu/aristeus/go/pkg/arisble"
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

type TrafficCount struct {
	LocationID            string
	Timestamp, ReceivedAt time.Time

	Duration          time.Duration
	Outgoing, Ingoing int
}

type ScaleReading struct {
	LocationID        string
	SensorID          string
	Timestamp         time.Time
	ReceivedAt        time.Time
	Total_kg          *float64
	Temperature_C     *float64
	Humidity_percent  *float64
	CellFrontLeft_kg  *float64
	CellFrontRight_kg *float64
	CellBackLeft_kg   *float64
	CellBackRight_kg  *float64
}

func (r *EnvironmentalReading) setData(data arisble.DataPoint) {
	r.Timestamp = data.Timestamp.ToTime()
	updateField := func(value float64, dest **float64) {
		if math.IsNaN(value) {
			*dest = nil
		} else {
			*dest = new(float64)
			**dest = value
		}
	}
	updateField(data.Temperature.Float64(), &r.Temperature_C)
	updateField(data.Humidity.Float64(), &r.Humidity_percent)
	updateField(data.Pressure.Float64(), &r.Pressure_hPa)
	if data.CO2 == arisble.CO2ConcentrationNaN {
		r.CO2_ppm = nil
	} else {
		r.CO2_ppm = new(uint)
		*r.CO2_ppm = uint(data.CO2)
	}
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
	SaveTrafficCount(ctx context.Context, counts []TrafficCount) error
	SaveScaleReading(ctx context.Context, readigns []ScaleReading) error
}

type DataJournalReader interface {
	Close() error

	GetEnvironmentalHistory(ctx context.Context, locationID string, start, end time.Time) ([]EnvironmentalReading, error)
	GetTrafficHistory(ctx context.Context, hiveID string, start, end time.Time) ([]TrafficCount, error)
	GetScaleHistory(ctx context.Context, hiveID string, start, end time.Time) ([]ScaleReading, error)
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
