package collector

import (
	"bytes"
	"encoding/json"
	"fmt"
	"io"
	"math"
	"strconv"
	"time"
)

type Timebase []time.Time
type FloatVector []float64

type EnvironmentalTimeSeries struct {
	LocationID       string      `json:"location_id"`
	Timestamp        Timebase    `json:"timestamps"`
	Temperature_C    FloatVector `json:"temperature_C"`
	Humidity_percent FloatVector `json:"humidity_percent"`
	Pressure_hPa     FloatVector `json:"pressure_hPa"`
	CO2_PPM          FloatVector `json:"co2_PPM"`
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

func (v FloatVector) MarshalJSON() ([]byte, error) {
	res := bytes.NewBuffer(nil)
	sep := "["
	for _, value := range v {
		if math.IsNaN(value) || math.IsInf(value, 1) || math.IsInf(value, -1) {
			res.WriteString(sep + "null")
		} else {
			res.Write([]byte(sep))
			res.Write(strconv.AppendFloat(nil, value, 'f', 3, 64))
		}
		sep = ","
	}
	res.Write([]byte("]"))
	return res.Bytes(), nil
}

func (v *FloatVector) UnmarshalJSON(data []byte) error {
	if bytes.Equal(data, []byte("null")) {
		*v = nil
		return nil
	}
	dec := json.NewDecoder(bytes.NewReader(data))
	tok, err := dec.Token()
	if err != nil {
		return err
	}
	if delim, ok := tok.(json.Delim); !ok || delim != '[' {
		return fmt.Errorf("expected '[' got %v", tok)
	}
	*v = nil
	for dec.More() {
		tok, err := dec.Token()
		if err != nil {
			return err
		}
		switch value := tok.(type) {
		case nil:
			*v = append(*v, math.NaN())
		case float64:
			*v = append(*v, value)
		default:
			return fmt.Errorf("unexpected token type %T in FloatVector", tok)
		}

	}

	tok, err = dec.Token()
	if err != nil {
		if err == io.EOF {
			return fmt.Errorf("unexpected EOF while parsing FloatVector")
		}
		return err
	}
	if delim, ok := tok.(json.Delim); !ok || delim != ']' {
		return fmt.Errorf("expected ']', got %v", tok)
	}
	return nil

}
