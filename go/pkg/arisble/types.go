package arisble

import (
	"encoding/binary"
	"errors"
	"fmt"
	"math"
	"strconv"
	"strings"
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
		return fmt.Errorf("unsuficient bytes len:%d (%d required)", len(d), size)
	}
	return nil
}

func (t *Temperature) UnmarshalBinary(d []byte) error {
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
	return strconv.FormatFloat(float64(h)/10, 'f', 1, 64) + "%"
}

func (h *Humidity) UnmarshalBinary(d []byte) error {
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

func NewPressure(value_hPa float64) Pressure {
	res := int64(value_hPa * 1000)
	if res > int64(PressureNaN) || value_hPa < 0.0 {
		return PressureNaN
	}
	return Pressure(res)
}

func (p Pressure) String() string {
	if p == PressureNaN {
		return "NaN"
	}
	return strconv.FormatFloat(float64(p)/1000.0, 'f', 3, 64) + "hPa"
}

func (p *Pressure) UnmarshalBinary(d []byte) error {
	if err := checkLength(d, 4); err != nil {
		return err
	}
	*p = Pressure(binary.LittleEndian.Uint32(d))
	return nil
}

func (p Pressure) MarshalBinary(d []byte) []byte {
	return binary.LittleEndian.AppendUint32(d, uint32(p))
}

func (p Pressure) Float64() float64 {
	if p == PressureNaN {
		return math.NaN()
	}
	return float64(p) / 1000.0
}

type Voltage uint16

const VoltageNaN = Voltage(0xFFFF)

func (v Voltage) String() string {
	if v == VoltageNaN {
		return "NaN"
	}
	return strconv.FormatFloat(float64(v)/1000.0, 'f', 3, 64) + "V"
}

func (v Voltage) Float64() float64 {
	if v == VoltageNaN {
		return math.NaN()
	}
	return float64(v) / 1000.0
}

func (p Pressure) ToVoltage() (loaded Voltage, open Voltage) {
	return Voltage(p >> 16), Voltage(p & 0xffff)
}

type CO2Concentration uint16

const CO2ConcentrationNaN = CO2Concentration(0xFFFF)

func (c CO2Concentration) String() string {
	if c == CO2ConcentrationNaN {
		return "NaN"
	}
	return strconv.FormatInt(int64(c), 10) + "PPM"
}

func (c *CO2Concentration) UnmarshalBinary(d []byte) error {
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

func (l *BatteryLevel) UnmarshalBinary(d []byte) error {
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

func (u *MemoryUsage) UnmarshalBinary(d []byte) error {
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

const PlacementUnset = 0
const (
	PlacementGeneral = 1 << 0
	PlacementOutside = 2 << 0
	PlacementInside  = 3 << 0
	PlacementVTop    = 1 << 2
	PlacementVCenter = 2 << 2
	PlacementVBottom = 3 << 2
	PlacementLLeft   = 1 << 4
	PlacementLCenter = 2 << 4
	PlacementLRight  = 3 << 4
	PlacementDBack   = 1 << 6
	PlacementDCenter = 2 << 6
	PlacementDFront  = 3 << 6
)

func (p Placement) Validate() bool {
	if p == PlacementGeneral {
		return true
	}
	if (p&0x03) == 0x01 && (p&0xF8) != 0x00 {
		return false
	}
	if (p&0x0c) == 0x00 || (p&0x30) == 0x00 || (p&0xc0) == 0x00 {
		return false
	}
	if p == PlacementOutside|PlacementDCenter|PlacementLCenter|PlacementVCenter {
		return false
	}
	return true
}

func ParsePlacement(desc string) (p Placement, err error) {
	defer func() {
		if err != nil {
			p = PlacementUnset
			err = fmt.Errorf("invalid placement '%s': %w", desc, err)
		}
	}()
	desc = strings.TrimSpace(desc)
	if len(desc) == 0 {
		return PlacementUnset, errors.New("empty")
	}

	if desc == "general" {
		return PlacementGeneral, nil
	}

	inside := PlacementGeneral
	lateral := PlacementLCenter
	vertical := PlacementVCenter

	parts := strings.Split(desc, " ")
	if len(parts) < 2 {
		return PlacementUnset, errors.New("non 'general' need at least two parts")
	}
	if len(parts) > 4 {
		return PlacementUnset, errors.New("must have at most 4 parts.")
	}

	switch parts[0] {
	case "inside":
		inside = PlacementInside
		break
	case "outside":
		inside = PlacementOutside
		break
	case "general":
		return PlacementUnset, errors.New("'general' must be used alone")
	default:
		return PlacementUnset, fmt.Errorf("invalid part '%s'", parts[0])
	}

	switch parts[1] {
	case "center":
		if len(parts) == 2 && inside == PlacementOutside {
			return PlacementUnset, errors.New("'outside center' is not possible")
		}
		vertical = PlacementVCenter
		break
	case "bottom":
		vertical = PlacementVBottom
		break
	case "top":
		vertical = PlacementVTop
		break
	case "left":
		lateral = PlacementLLeft
		break
	case "right":
		lateral = PlacementLRight
		break
	case "front":
		if len(parts) > 2 {
			return PlacementUnset, errors.New("extra parts after 'front'")
		}
		return Placement(inside | vertical | lateral | PlacementDFront), nil
	case "back":
		if len(parts) > 2 {
			return PlacementUnset, errors.New("extra parts after 'back'")
		}
		return Placement(inside | vertical | lateral | PlacementDBack), nil
	default:
		return PlacementUnset, fmt.Errorf("invalid part '%s'", parts[1])
	}

	if len(parts) == 2 {
		return Placement(inside | vertical | lateral | PlacementDCenter), nil
	}

	switch parts[2] {
	case "center":
		lateral = PlacementLCenter
		break
	case "bottom":
		return PlacementUnset, errors.New("'bottom' can only be used in second position")
	case "top":
		return PlacementUnset, errors.New("'top' can only be used in second position")
	case "left":
		lateral = PlacementLLeft
		break
	case "right":
		lateral = PlacementLRight
		break
	case "front":
		if len(parts) > 3 {
			return PlacementUnset, errors.New("extra parts after 'front'")
		}
		return Placement(inside | vertical | lateral | PlacementDFront), nil
	case "back":
		if len(parts) > 3 {
			return PlacementUnset, errors.New("extra parts after 'back'")
		}
		return Placement(inside | vertical | lateral | PlacementDBack), nil
	default:
		return PlacementUnset, fmt.Errorf("invalid part '%s'", parts[2])
	}

	if len(parts) == 3 {
		return Placement(inside | vertical | lateral | PlacementDCenter), nil
	}

	switch parts[3] {
	case "center":
		return Placement(inside | vertical | lateral | PlacementDCenter), nil
	case "bottom":
		return PlacementUnset, errors.New("'bottom' can only be used in second position")
	case "top":
		return PlacementUnset, errors.New("'top' can only be used in second position")
	case "left":
		return PlacementUnset, errors.New("'bottom' can only be used in third position")
	case "right":
		return PlacementUnset, errors.New("'bottom' can only be used in third position")
	case "front":
		return Placement(inside | vertical | lateral | PlacementDFront), nil
	case "back":
		return Placement(inside | vertical | lateral | PlacementDBack), nil
	default:
		return PlacementUnset, fmt.Errorf("invalid part '%s'", parts[3])
	}

}

func (p Placement) String() string {
	if p == PlacementUnset {
		return "UNSET"
	}

	if p == PlacementGeneral {
		return "general"
	}

	if p.Validate() == false {
		return "INVALID"
	}

	if p == PlacementInside|PlacementDCenter|PlacementLCenter|PlacementVCenter {
		return "inside center"
	}

	var res string
	if (p & 0x03) == PlacementOutside {
		res += "outside"
	} else {
		res += "inside"
	}

	switch p & 0x0c {
	case PlacementVCenter:
		break
	case PlacementVTop:
		res += " top"
		break
	case PlacementVBottom:
		res += " bottom"
		break
	}

	switch p & 0x30 {
	case PlacementLCenter:
		break
	case PlacementLLeft:
		res += " left"
		break
	case PlacementLRight:
		res += " right"
		break
	}

	switch p & 0xc0 {
	case PlacementDCenter:
		break
	case PlacementDBack:
		res += " back"
		break
	case PlacementDFront:
		res += " front"
		break
	}

	return res
}

type Location struct {
	HiveID    uint8
	Placement Placement
}

func (l Location) String() string {
	return "Hive:" + strconv.FormatInt(int64(l.HiveID), 10) + " Placement:" + l.Placement.String()
}

func (l *Location) UnmarshalBinary(d []byte) error {
	if err := checkLength(d, 2); err != nil {
		return err
	}
	l.HiveID = d[0]
	l.Placement = Placement(d[1])
	return nil
}

func (l Location) MarshalBinary(d []byte) []byte {
	d = append(d, l.HiveID)
	d = append(d, byte(l.Placement))
	return d
}

type Timestamp uint32

const TimestampNaN = Timestamp(0xFFFFFFFF)

func NewTimestamp(t time.Time) Timestamp {
	ts := t.Unix()
	if ts < 0 || ts > int64(TimestampNaN) {
		return TimestampNaN
	}
	return Timestamp(ts)
}

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

func (t Timestamp) AppendBinary(d []byte) []byte {
	return binary.LittleEndian.AppendUint32(d, uint32(t))
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
	if err := d.Timestamp.UnmarshalBinary(data[0:4]); err != nil { // coverage-ignore
		return err
	}
	if err := d.Temperature.UnmarshalBinary(data[4:6]); err != nil { // coverage-ignore
		return err
	}
	if err := d.Humidity.UnmarshalBinary(data[6:8]); err != nil { // coverage-ignore
		return err
	}
	if err := d.Pressure.UnmarshalBinary(data[8:12]); err != nil { // coverage-ignore
		return err
	}
	if err := d.CO2.UnmarshalBinary(data[12:14]); err != nil { // coverage-ignore

		return err
	}
	return nil
}

func (d DataPoint) MarshalBinary(data []byte) []byte {
	data = d.Timestamp.AppendBinary(data)
	data = binary.LittleEndian.AppendUint16(data, uint16(d.Temperature))
	data = binary.LittleEndian.AppendUint16(data, uint16(d.Humidity))
	data = binary.LittleEndian.AppendUint32(data, uint32(d.Pressure))
	data = binary.LittleEndian.AppendUint16(data, uint16(d.CO2))
	return data
}

type AdvertisementData struct {
	Location     Location
	Battery      BatteryLevel
	Memory       MemoryUsage
	CurrentPoint DataPoint
}

func (adv *AdvertisementData) UnmarshalBinary(data []byte) error {
	if err := checkLength(data, 18); err != nil {
		return err
	}
	if err := adv.Location.UnmarshalBinary(data[0:2]); err != nil { // coverage-ignore
		return err
	}
	if err := adv.Battery.UnmarshalBinary(data[2:3]); err != nil { // coverage-ignore
		return err
	}
	if err := adv.Memory.UnmarshalBinary(data[3:4]); err != nil { // coverage-ignore
		return err
	}
	if err := adv.CurrentPoint.UnmarshalBinary(data[4:18]); err != nil { // coverage-ignore
		return err
	}
	return nil
}

func (adv *AdvertisementData) MarshalBinary(data []byte) []byte {
	data = adv.Location.MarshalBinary(data)
	data = append(data, byte(adv.Battery), byte(adv.Memory))
	return adv.CurrentPoint.MarshalBinary(data)
}
