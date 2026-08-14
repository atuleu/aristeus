package arisble

import (
	"encoding/binary"
	"fmt"
	"math"
	"strconv"
	"time"
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

type BatteryLevel uint8

const BatteryLevelNaN = BatteryLevel(0xFF)

func (l BatteryLevel) String() string {

	if l > 100 {
		return "NaN"
	}
	return strconv.FormatInt(int64(l), 10) + "%"
}

func (l *BatteryLevel) BinaryUnmarshal(d []byte) error {
	if err := checkLength(d, 1); err != nil {
		return err
	}
	*l = BatteryLevel(d[0])
	return nil
}

func (l BatteryLevel) Float64() float64 {
	if l > 100 {
		return math.NaN()
	}
	return float64(l)
}

type MemoryUsage uint8

func (u MemoryUsage) String() string {
	return strconv.FormatFloat(100.0*float64(u)/255.0, 'f', 1, 64) + "%"
}

func (u *MemoryUsage) BinaryUnmarshal(d []byte) error {
	if err := checkLength(d, 1); err != nil {
		return err
	}
	*u = MemoryUsage(d[0])
	return nil
}

func (u MemoryUsage) Float64() float64 {
	return 100.0 * float64(u) / 255.0
}

type Placement uint8

const PlacementGeneral = 0
const (
	PlacementOutside = 1 << iota
	PlacementInside
	PlacementTop
	PlacementFront
	PlacementLeft
)

func (p Placement) String() string {
	if p == PlacementGeneral {
		return "general"
	}
	if (p&0x03) == 0x03 || (p&0x03) == 0x00 {
		return "INVALID"
	}
	var res string
	if (p & PlacementTop) != 0x00 {
		res = "top"
	} else {
		res = "bottom"
	}

	if (p & PlacementFront) != 0x00 {
		res += " front"
	} else {
		res += " back"
	}

	if (p & PlacementLeft) != 0x00 {
		res += " left"
	} else {
		res += " righ"
	}

	if (p & PlacementInside) != 0x00 {
		res += " inside"
	}

	if (p & PlacementOutside) != 0x00 {
		res += " outside"
	}

	return res
}

type Location struct {
	HiveID    uint8
	Placement Placement
}

func (l Location) String() string {
	return "Hive:" + strconv.FormatInt(int64(l.HiveID), 10) + " Placement: " + l.Placement.String()
}

func (l *Location) UnmarshalBinary(d []byte) error {
	if err := checkLength(d, 2); err != nil {
		return err
	}
	l.HiveID = d[0]
	l.Placement = Placement(d[1])
	return nil
}

type Timestamp uint32

func (t Timestamp) ToTime() time.Time {
	return time.Unix(int64(t), 0)
}

func (t Timestamp) String() string {
	return t.ToTime().String()
}

func (t *Timestamp) UnmarshalBinary(d []byte) error {
	if err := checkLength(d, 4); err != nil {
		return err
	}
	*t = Timestamp(binary.LittleEndian.Uint32(d))
	return nil
}

type DataPoint struct {
	Timestamp   Timestamp
	Temperature Temperature
	Humidity    Humidity
	Pressure    Pressure
	CO2         CO2Concentration
}

func (d *DataPoint) UnmarshalBinary(data []byte) error {
	if err := checkLength(data, 14); err != nil {
		return err
	}
	if err := d.Timestamp.UnmarshalBinary(data[0:4]); err != nil {
		return err
	}
	if err := d.Temperature.BinaryUnmarshal(data[4:6]); err != nil {
		return err
	}
	if err := d.Humidity.BinaryUnmarshal(data[6:8]); err != nil {
		return err
	}
	if err := d.Pressure.BinaryUnmarshal(data[8:12]); err != nil {
		return err
	}
	if err := d.CO2.BinaryUnmarshal(data[12:14]); err != nil {
		return err
	}
	return nil
}

type AdvertisementData struct {
	Location     Location
	Battery      BatteryLevel
	Memory       MemoryUsage
	CurrentPoint DataPoint
}

func (adv *AdvertisementData) UnmarshalBinary(data []byte) error {
	if err := checkLength(data, 20); err != nil {
		return err
	}
	if err := adv.Location.UnmarshalBinary(data[0:2]); err != nil {
		return err
	}
	if err := adv.Battery.BinaryUnmarshal(data[2:3]); err != nil {
		return err
	}
	if err := adv.Memory.BinaryUnmarshal(data[3:4]); err != nil {
		return err
	}
	if err := adv.CurrentPoint.UnmarshalBinary(data[4:18]); err != nil {
		return err
	}
	return nil
}
