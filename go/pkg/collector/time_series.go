package collector

import (
	"bytes"
	"encoding/json"
	"strconv"
	"time"
)

type Timebase []time.Time
type FloatVector []*float64
type UintVector []*uint
type EnvironmentalTimeSeries struct {
	LocationID       string      `json:"location_id"`
	Timestamp        Timebase    `json:"timestamps"`
	Temperature_C    FloatVector `json:"temperature_C"`
	Humidity_percent FloatVector `json:"humidity_percent"`
	Pressure_hPa     FloatVector `json:"pressure_hPa"`
	CO2_PPM          UintVector  `json:"co2_PPM"`
}

func (t Timebase) MarshalJSON() ([]byte, error) {
	res := bytes.NewBuffer(nil)
	sep := "["
	for _, ts := range t {
		res.Write([]byte(sep))
		res.Write(strconv.AppendInt(nil, ts.UnixMilli(), 10))
		sep = ","
	}
	res.Write([]byte("]"))
	return res.Bytes(), nil
}

func (t *Timebase) UnmarshalJSON(bytes []byte) error {
	asInt := []int64{}
	err := json.Unmarshal(bytes, &asInt)
	if err != nil {
		return err
	}
	*t = make([]time.Time, 0, len(asInt))
	for _, ts := range asInt {
		*t = append(*t, time.UnixMilli(ts))
	}
	return nil
}
