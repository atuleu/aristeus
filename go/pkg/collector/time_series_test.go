package collector

import (
	"encoding/json"
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
		Temperature_C:    []*float64{newValue(22.0), newValue(22.1), nil},
		Humidity_percent: []*float64{newValue(33.0), newValue(34.1), newValue(44.0)},
		Pressure_hPa:     []*float64{newValue(1013.0), newValue(1013.1), nil},
		CO2_PPM:          []*uint{newValue(uint(456)), newValue(uint(443)), nil},
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
    22,
    22.1,
    null
  ],
  "humidity_percent": [
    33,
    34.1,
    44
  ],
  "pressure_hPa": [
    1013,
    1013.1,
    null
  ],
  "co2_PPM": [
    456,
    443,
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
