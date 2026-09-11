package collector

import (
	"encoding/json"
	"testing"
	"time"

	"github.com/stretchr/testify/assert"
)

func TestEnvironmentalDeviceJSONFormatting(t *testing.T) {
	testdata := []struct {
		name     string
		input    EnvironmentalDevice
		expected string
	}{
		{
			name: "defined",
			input: EnvironmentalDevice{
				Location: SensorLocation{
					LocationID:  "hive_001_general",
					HiveID:      "hive_001",
					Description: "hive 1 general",
				},
				Address:             "02:02:02:02:02:02",
				AdvertisementPeriod: 501 * time.Millisecond,
				TimeOffset:          340 * time.Millisecond,
				LastSeen:            time.Date(2026, 1, 1, 0, 0, 0, 0, time.UTC),
				Battery:             newValue(99.0),
				MemoryUsage:         newValue(0.1),
				Current: EnvironmentalState{
					Timestamp:        time.Date(2026, 1, 1, 0, 0, 0, 0, time.UTC),
					Temperature_C:    newValue(22.2),
					Humidity_percent: newValue(33.3),
					Pressure_hPa:     newValue(1013.2),
					CO2_PPM:          newValue(uint(453)),
				},
			},
			expected: `{
  "location":{
    "location_id":"hive_001_general",
    "hive_id":"hive_001",
    "description":"hive 1 general"
  },
  "address":"02:02:02:02:02:02",
  "time_offset_s":0.340,
  "advertisement_period_ms": 501,
  "last_seen": "2026-01-01T00:00:00Z",
  "battery": 99,
  "memory_usage": 0.1,
  "current_state": {
    "timestamp": "2026-01-01T00:00:00Z",
    "temperature_C": 22.2,
    "humidity_percent": 33.3,
    "pressure_hPa": 1013.2,
    "co2_PPM": 453
  }
}`},
		{name: "empty", expected: `{
  "location":{
    "location_id":"",
    "hive_id":"",
    "description":""
  },
  "address":"",
  "time_offset_s":0,
  "advertisement_period_ms": 0,
  "last_seen": "0001-01-01T00:00:00Z",
  "battery": null,
  "memory_usage": null,
  "current_state": {
    "timestamp": "0001-01-01T00:00:00Z",
    "temperature_C": null,
    "humidity_percent": null,
    "pressure_hPa": null,
    "co2_PPM": null
  }
}`},
	}

	for _, d := range testdata {
		t.Run(d.name, func(t *testing.T) {
			assert := assert.New(t)
			asJSON, err := json.Marshal(d.input)
			assert.NoError(err)
			assert.JSONEq(d.expected, string(asJSON))
		})
	}

}
