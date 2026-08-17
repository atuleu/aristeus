package arisble

import (
	"fmt"
	"math"
	"testing"
	"time"

	"github.com/stretchr/testify/assert"
)

func TestTemperatureFormatting(t *testing.T) {
	testdata := []struct {
		value           Temperature
		expected        string
		expectedAsFloat float64
	}{
		{TemperatureNaN, "NaN", math.NaN()},
		{Temperature(1000), "10.00°C", 10.0},
		{Temperature(-3245), "-32.45°C", -32.45},
	}

	for _, d := range testdata {
		t.Run(d.expected, func(t *testing.T) {
			assert := assert.New(t)
			assert.Equal(d.expected, d.value.String())
			assert.InDelta(d.expectedAsFloat, d.value.Float64(), 0.005)
		})
	}
}

func TestTemperatureParsing(t *testing.T) {
	testdata := []struct {
		name     string
		input    []byte
		expected Temperature
		error    error
	}{
		{"NaN", []byte{0x00, 0x80}, TemperatureNaN, nil},
		{"1000", []byte{0xe8, 0x03}, Temperature(1000), nil},
		{"wrong", []byte{0xe8}, TemperatureNaN, fmt.Errorf("unsuficient bytes len:1 (2 required)")},
	}

	for _, d := range testdata {
		t.Run(d.name, func(t *testing.T) {
			assert := assert.New(t)
			var res Temperature
			if d.error != nil {
				assert.ErrorContains(res.UnmarshalBinary(d.input), d.error.Error())
			} else {
				assert.Nil(res.UnmarshalBinary(d.input))
				assert.Equal(d.expected, res)
			}
		})
	}
}

func TestHumidityFormatting(t *testing.T) {
	testdata := []struct {
		value           Humidity
		expected        string
		expectedAsFloat float64
	}{
		{HumidityNaN, "NaN", math.NaN()},
		{Humidity(1000), "100.0%", 100.0},
		{Humidity(213), "21.3%", 21.3},
	}

	for _, d := range testdata {
		t.Run(d.expected, func(t *testing.T) {
			assert := assert.New(t)
			assert.Equal(d.expected, d.value.String())
			assert.InDelta(d.expectedAsFloat, d.value.Float64(), 0.005)
		})
	}
}

func TestHumidityParsing(t *testing.T) {
	testdata := []struct {
		name     string
		input    []byte
		expected Humidity
		error    error
	}{
		{"NaN", []byte{0xff, 0xff}, HumidityNaN, nil},
		{"1000", []byte{0xe8, 0x03}, Humidity(1000), nil},
		{"wrong", []byte{0xe8}, HumidityNaN, fmt.Errorf("unsuficient bytes len:1 (2 required)")},
	}

	for _, d := range testdata {
		t.Run(d.name, func(t *testing.T) {
			assert := assert.New(t)
			var res Humidity
			if d.error != nil {
				assert.ErrorContains(res.UnmarshalBinary(d.input), d.error.Error())
			} else {
				assert.Nil(res.UnmarshalBinary(d.input))
				assert.Equal(d.expected, res)
			}
		})
	}
}

func TestPressureFormatting(t *testing.T) {
	testdata := []struct {
		value           Pressure
		expected        string
		expectedAsFloat float64
	}{
		{PressureNaN, "NaN", math.NaN()},
		{Pressure(1000), "1.000hPa", 1.000},
		{Pressure(1015230), "1015.230hPa", 1015.23},
	}

	for _, d := range testdata {
		t.Run(d.expected, func(t *testing.T) {
			assert := assert.New(t)
			assert.Equal(d.expected, d.value.String())
			assert.InDelta(d.expectedAsFloat, d.value.Float64(), 0.005)
		})
	}
}

func TestPressureParsing(t *testing.T) {
	testdata := []struct {
		name     string
		input    []byte
		expected Pressure
		error    error
	}{
		{"NaN", []byte{0xff, 0xff, 0xff, 0xff}, PressureNaN, nil},
		{"1000", []byte{0xe8, 0x03, 0x00, 0x00}, Pressure(1000), nil},
		{"wrong", []byte{0xe8, 0x03, 0x00}, PressureNaN, fmt.Errorf("unsuficient bytes len:3 (4 required)")},
	}

	for _, d := range testdata {
		t.Run(d.name, func(t *testing.T) {
			assert := assert.New(t)
			var res Pressure
			if d.error != nil {
				assert.ErrorContains(res.UnmarshalBinary(d.input), d.error.Error())
			} else {
				assert.Nil(res.UnmarshalBinary(d.input))
				assert.Equal(d.expected, res)
			}
		})
	}
}

func TestCO2Formatting(t *testing.T) {
	testdata := []struct {
		value           CO2Concentration
		expected        string
		expectedAsFloat float64
	}{
		{CO2ConcentrationNaN, "NaN", math.NaN()},
		{CO2Concentration(1000), "1000PPM", 1000.0},
		{CO2Concentration(432), "432PPM", 432.0},
	}

	for _, d := range testdata {
		t.Run(d.expected, func(t *testing.T) {
			assert := assert.New(t)
			assert.Equal(d.expected, d.value.String())
			assert.InDelta(d.expectedAsFloat, d.value.Float64(), 0.005)
		})
	}
}

func TestCO2Parsing(t *testing.T) {
	testdata := []struct {
		name     string
		input    []byte
		expected CO2Concentration
		error    error
	}{
		{"NaN", []byte{0xff, 0xff}, CO2ConcentrationNaN, nil},
		{"1000", []byte{0xe8, 0x03, 0x00, 0x00}, CO2Concentration(1000), nil},
		{"wrong", []byte{0xe8}, CO2ConcentrationNaN, fmt.Errorf("unsuficient bytes len:1 (2 required)")},
	}

	for _, d := range testdata {
		t.Run(d.name, func(t *testing.T) {
			assert := assert.New(t)
			var res CO2Concentration
			if d.error != nil {
				assert.ErrorContains(res.UnmarshalBinary(d.input), d.error.Error())
			} else {
				assert.Nil(res.UnmarshalBinary(d.input))
				assert.Equal(d.expected, res)
			}
		})
	}
}

func TestBatteryFormatting(t *testing.T) {
	testdata := []struct {
		value           BatteryLevel
		expected        string
		expectedAsFloat float64
	}{
		{BatteryLevelNaN, "NaN", math.NaN()},
		{BatteryLevel(100), "100%", 100.0},
		{BatteryLevel(22), "22%", 22.0},
	}

	for _, d := range testdata {
		t.Run(d.expected, func(t *testing.T) {
			assert := assert.New(t)
			assert.Equal(d.expected, d.value.String())
			assert.InDelta(d.expectedAsFloat, d.value.Float64(), 0.005)
		})
	}
}

func TestBatteryParsing(t *testing.T) {
	testdata := []struct {
		name     string
		input    []byte
		expected BatteryLevel
		error    error
	}{
		{"NaN", []byte{0xff}, BatteryLevelNaN, nil},
		{"1000", []byte{0x0a}, BatteryLevel(10), nil},
		{"wrong", []byte{}, BatteryLevelNaN, fmt.Errorf("unsuficient bytes len:0 (1 required)")},
	}

	for _, d := range testdata {
		t.Run(d.name, func(t *testing.T) {
			assert := assert.New(t)
			var res BatteryLevel
			if d.error != nil {
				assert.ErrorContains(res.UnmarshalBinary(d.input), d.error.Error())
			} else {
				assert.Nil(res.UnmarshalBinary(d.input))
				assert.Equal(d.expected, res)
			}
		})
	}
}

func TestMemoryFormatting(t *testing.T) {
	testdata := []struct {
		value           MemoryUsage
		expected        string
		expectedAsFloat float64
	}{
		{MemoryUsage(255), "100.0%", 100.0},
		{MemoryUsage(127), "49.8%", 49.8},
	}

	for _, d := range testdata {
		t.Run(d.expected, func(t *testing.T) {
			assert := assert.New(t)
			assert.Equal(d.expected, d.value.String())
			assert.InDelta(d.expectedAsFloat, d.value.Float64(), 0.005)
		})
	}
}

func TestMemoryParsing(t *testing.T) {
	testdata := []struct {
		name     string
		input    []byte
		expected MemoryUsage
		error    error
	}{
		{"50%", []byte{0x80}, MemoryUsage(128), nil},
		{"wrong", []byte{}, MemoryUsage(0), fmt.Errorf("unsuficient bytes len:0 (1 required)")},
	}

	for _, d := range testdata {
		t.Run(d.name, func(t *testing.T) {
			assert := assert.New(t)
			var res MemoryUsage
			if d.error != nil {
				assert.ErrorContains(res.UnmarshalBinary(d.input), d.error.Error())
			} else {
				assert.Nil(res.UnmarshalBinary(d.input))
				assert.Equal(d.expected, res)
			}
		})
	}
}

func TestPlacementFormatting(t *testing.T) {
	testdata := []struct {
		value    Placement
		expected string
	}{
		{PlacementGeneral, "general"},
		{PlacementInside | PlacementOutside, "INVALID"},
		{PlacementInside, "bottom back inside right"},
		{PlacementOutside | PlacementFront | PlacementTop | PlacementLeft, "top front outside left"},
	}

	for _, d := range testdata {
		t.Run(d.expected, func(t *testing.T) {
			assert := assert.New(t)
			assert.Equal(d.expected, d.value.String())
		})
	}
}

func TestLocationFormatting(t *testing.T) {
	testdata := []struct {
		value    Location
		expected string
	}{
		{Location{0, PlacementGeneral}, "Hive:0 Placement:general"},
	}

	for _, d := range testdata {
		t.Run(d.expected, func(t *testing.T) {
			assert := assert.New(t)
			assert.Equal(d.expected, d.value.String())
		})
	}
}

func TestLocationParsing(t *testing.T) {
	testdata := []struct {
		name     string
		input    []byte
		expected Location
		error    error
	}{
		{"general inside", []byte{0x0, 0x01}, Location{HiveID: 0, Placement: PlacementOutside}, nil},
		{"wrong", []byte{0x42}, Location{}, fmt.Errorf("unsuficient bytes len:1 (2 required)")},
	}

	for _, d := range testdata {
		t.Run(d.name, func(t *testing.T) {
			assert := assert.New(t)
			var res Location
			if d.error != nil {
				assert.ErrorContains(res.UnmarshalBinary(d.input), d.error.Error())
			} else {
				assert.Nil(res.UnmarshalBinary(d.input))
				assert.Equal(d.expected, res)
			}
		})
	}
}

func TestTimestampFormatting(t *testing.T) {
	testdata := []struct {
		value    Timestamp
		expected string
	}{
		{0xFFFFFFFF, time.Unix(0xFFFFFFFF, 0).String()},
		{0x0, time.Unix(0, 0).String()},
	}

	for _, d := range testdata {
		t.Run(d.expected, func(t *testing.T) {
			assert := assert.New(t)
			assert.Equal(d.expected, d.value.String())
		})
	}
}

var timestamp Timestamp

func init() {

	tt, err := time.Parse(time.RFC3339, "2026-01-01T00:00:00Z")
	if err != nil {
		panic(err.Error())
	}
	timestamp = Timestamp(tt.Unix())
}

func TestTimestampParsing(t *testing.T) {
	testdata := []struct {
		name     string
		input    []byte
		expected Timestamp
		error    error
	}{
		{"2026-01-01T00:00:00.000Z", []byte{0x00, 0xb9, 0x55, 0x69}, timestamp, nil},
		{"wrong", []byte{0x01}, Timestamp(0xFFFFFFFF), fmt.Errorf("unsuficient bytes len:1 (4 required)")},
	}

	for _, d := range testdata {
		t.Run(d.name, func(t *testing.T) {
			assert := assert.New(t)
			var res Timestamp
			if d.error != nil {
				assert.ErrorContains(res.UnmarshalBinary(d.input), d.error.Error())
			} else {
				assert.Nil(res.UnmarshalBinary(d.input))
				assert.Equal(d.expected, res)
			}
		})
	}
}

func TestAdvertismentParsing(t *testing.T) {
	testdata := []struct {
		name     string
		input    []byte
		expected AdvertisementData
		error    error
	}{
		{"wrong", []byte{0x01}, AdvertisementData{}, fmt.Errorf("unsuficient bytes len:1 (18 required)")},
		{
			"good",
			[]byte{0x01, 0x01, 0x0a, 0x00, 0x00, 0xb9, 0x55, 0x69, 0xe8, 0x03, 0xf4, 0x01, 0x80, 0x61, 0x0f, 0x00, 0x90, 0x01},
			AdvertisementData{
				Location: Location{1, PlacementOutside},
				Battery:  BatteryLevel(10),
				Memory:   MemoryUsage(0),
				CurrentPoint: DataPoint{
					Timestamp:   timestamp,
					Temperature: Temperature(1000),
					Humidity:    Humidity(500),
					Pressure:    Pressure(1008000),
					CO2:         CO2Concentration(400),
				},
			},
			nil,
		},
	}

	for _, d := range testdata {
		t.Run(d.name, func(t *testing.T) {
			assert := assert.New(t)
			var res AdvertisementData
			if d.error != nil {
				assert.ErrorContains(res.UnmarshalBinary(d.input), d.error.Error())
			} else {
				assert.Nil(res.UnmarshalBinary(d.input))
				assert.Equal(d.expected, res)
			}
		})
	}
}
