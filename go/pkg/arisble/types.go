package arisble

import (
	"encoding/binary"
	"fmt"
	"math"
	"strconv"
)

type Temperature int16

const TemperatureNaN = Temperature(-0x8000)

func (t Temperature) String() string {
	if t == TemperatureNaN {
		return "NaN"
	}

	return strconv.FormatFloat(float64(t)/100, 'f', 2, 64) + "°C"
}

func checkLength(d []byte, size int) error {
	if len(d) < size {
		return fmt.Errorf("unsuficient bytes %d (%d required)", len(d), size)
	}
	return nil
}

func (t *Temperature) BinaryUnmarshal(d []byte) error {
	if err := checkLength(d, 2); err != nil {
		return err
	}
	*t = Temperature(binary.LittleEndian.Uint16(d))
	return nil
}

func (t Temperature) Float64() float64 {
	if t == TemperatureNaN {
		return math.NaN()
	}
	return float64(t) / 100.0
}

type Humidity uint16

const HumidityNaN = Humidity(0xFFFF)

func (h Humidity) String() string {
	if h == HumidityNaN {
		return "NaN"
	}
	return strconv.FormatFloat(float64(h)/10, 'f', 1, 64)
}

func (h *Humidity) BinaryUnmarshal(d []byte) error {
	if err := checkLength(d, 2); err != nil {
		return err
	}
	*h = Humidity(binary.LittleEndian.Uint16(d))
	return nil
}

func (h Humidity) Float64() float64 {
	if h == HumidityNaN {
		return math.NaN()
	}
	return float64(h) / 10.0
}

type Pressure uint32

const PressureNaN = Pressure(0xFFFFFFFF)

func (p Pressure) String() string {
	if p == PressureNaN {
		return "NaN"
	}
	return strconv.FormatFloat(float64(p)/1000.0, 'f', 3, 64) + "hPa"
}

func (p *Pressure) BinaryUnmarshal(d []byte) error {
	if err := checkLength(d, 4); err != nil {
		return err
	}
	*p = Pressure(binary.LittleEndian.Uint32(d))
	return nil
}

func (p Pressure) Float64() float64 {
	if p == PressureNaN {
		return math.NaN()
	}
	return float64(p) / 1000.0
}

type CO2Concentration uint16

const CO2ConcentrationNaN = CO2Concentration(0xFFFF)

func (c CO2Concentration) String() string {
	if c == CO2ConcentrationNaN {
		return "NaN"
	}
	return strconv.FormatInt(int64(c), 10) + "PPM"
}

func (c *CO2Concentration) BinaryUnmarshal(d []byte) error {
	if err := checkLength(d, 2); err != nil {
		return err
	}
	*c = CO2Concentration(binary.LittleEndian.Uint16(d))
	return nil
}

func (c CO2Concentration) Float64() float64 {
	if c == CO2ConcentrationNaN {
		return math.NaN()
	}
	return float64(c)
}
