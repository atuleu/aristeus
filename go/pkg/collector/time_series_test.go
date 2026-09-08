package collector

import (
	"encoding/json"
	"math"
	"testing"
	"time"

	"github.com/google/go-cmp/cmp"
	"github.com/google/go-cmp/cmp/cmpopts"
	"github.com/stretchr/testify/suite"
)

type TimeSeriesSuite struct {
	suite.Suite
}

func (s *TimeSeriesSuite) TestJSONFormatting() {
	assert := s.Assert()
	require := s.Require()

	a := time.Unix(0, 0)
	b := a.Add(1 * time.Minute)
	c := b.Add(1*time.Minute + 1*time.Second)

	data := EnvironmentalTimeSeries{
		LocationID:       "foo",
		Timestamp:        []time.Time{a, b, c},
		Temperature_C:    []float64{22.0, 22.1, math.NaN()},
		Humidity_percent: []float64{33, 34.1, 44},
		Pressure_hPa:     []float64{1013.0, 1013.1, math.NaN()},
		CO2_PPM:          []float64{456, 443, math.NaN()},
	}

	asJSON, err := json.MarshalIndent(data, "", "  ")
	require.NoError(err)
	assert.Equal(`{
  "location_id": "foo",
  "timestamps": [
    0,
    60000,
    121000
  ],
  "temperature_C": [
    22.000,
    22.100,
    null
  ],
  "humidity_percent": [
    33.000,
    34.100,
    44.000
  ],
  "pressure_hPa": [
    1013.000,
    1013.100,
    null
  ],
  "co2_PPM": [
    456.000,
    443.000,
    null
  ]
}`, string(asJSON))

	var parsed EnvironmentalTimeSeries
	err = json.Unmarshal(asJSON, &parsed)
	require.NoError(err)

	diff := cmp.Diff(data, parsed, cmpopts.EquateNaNs())
	assert.Equal("", diff)

}

func TestTimeSeriesSuite(t *testing.T) {
	suite.Run(t, new(TimeSeriesSuite))
}
