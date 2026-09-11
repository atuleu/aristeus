package collector

import (
	"encoding/json"
	"errors"
	"fmt"
	"math"
	"strconv"
	"strings"
	"time"

	"github.com/atuleu/aristeus/go/pkg/arisble"
	"github.com/go-ble/ble"
)

type EnvironmentalAdvertisment struct {
	address    ble.Addr
	data       arisble.AdvertisementData
	receivedAt time.Time
}

type EnvironmentalState struct {
	Timestamp        time.Time `json:"timestamp"`
	Temperature_C    *float64  `json:"temperature_C"`
	Humidity_percent *float64  `json:"humidity_percent"`
	Pressure_hPa     *float64  `json:"pressure_hPa"`
	CO2_PPM          *uint     `json:"co2_PPM"`
}

type EnvironmentalDevice struct {
	location            arisble.Location
	assignedSince       time.Time
	Location            SensorLocation     `json:"location"`
	Address             string             `json:"address"`
	TimeOffset          time.Duration      `json:"-"`
	LastSeen            time.Time          `json:"last_seen"`
	AdvertisementPeriod time.Duration      `json:"-"`
	Battery             *float64           `json:"battery"`
	MemoryUsage         *float64           `json:"memory_usage"`
	Current             EnvironmentalState `json:"current_state"`
}

func (d EnvironmentalDevice) MarshalJSON() ([]byte, error) {
	type Alias EnvironmentalDevice
	return json.Marshal(&struct {
		Alias
		TimeOffset_s           float64 `json:"time_offset_s"`
		AdvertisementPeriod_ms int64   `json:"advertisement_period_ms"`
	}{
		Alias:                  (Alias)(d),
		TimeOffset_s:           d.TimeOffset.Seconds(),
		AdvertisementPeriod_ms: d.AdvertisementPeriod.Milliseconds(),
	})
}

func BuildLocationID(l arisble.Location) string {
	return fmt.Sprintf(
		"hive_%03d_%s",
		l.HiveID,
		strings.Replace(l.Placement.String(), " ", "_", -1),
	)
}

func BuildSensorLocation(l arisble.Location) SensorLocation {
	locationID := BuildLocationID(l)
	return SensorLocation{
		LocationID:  BuildLocationID(l),
		HiveID:      locationID[0:8],
		Description: fmt.Sprintf("hive %03d %s", l.HiveID, l.Placement),
	}
}

func ParseLocationID(locationID string) (l arisble.Location, err error) {
	defer func() {
		if err != nil {
			err = fmt.Errorf("invalid location_id=%s: %w", locationID, err)
		}
	}()
	if len(locationID) < 9 {
		return arisble.Location{}, fmt.Errorf("invalid lenght=%d, minimum: 9", len(locationID))
	}
	if strings.HasPrefix(locationID, "hive_") == false {
		return arisble.Location{}, errors.New("missing prefix 'hive_'")
	}
	if locationID[8] != '_' {
		return arisble.Location{}, errors.New("missing separator '_'")
	}
	hiveID, err := strconv.ParseInt(locationID[5:8], 10, 8)
	if err != nil {
		return
	}
	l.HiveID = uint8(hiveID)
	l.Placement, err = arisble.ParsePlacement(strings.Replace(locationID[9:], "_", " ", -1))
	return
}

func NewAssignedEnvironmentalDevice(assignement SensorAssignement) (*EnvironmentalDevice, error) {
	location, err := ParseLocationID(assignement.LocationID)
	if err != nil {
		return nil, err
	}
	return &EnvironmentalDevice{
		location:      location,
		assignedSince: assignement.InstalledAt,
		Location:      BuildSensorLocation(location),
		Address:       assignement.SensorID,
		Current:       EnvironmentalState{},
	}, nil
}

func setFloat(v float64) *float64 {
	if math.IsNaN(v) {
		return nil
	}
	return newValue(v)
}

func setCO2(v arisble.CO2Concentration) *uint {
	if v == arisble.CO2ConcentrationNaN {
		return nil
	}
	return newValue(uint(v))
}

func NewEnvironmentalDevice(adv EnvironmentalAdvertisment) (*EnvironmentalDevice, error) {
	if adv.data.Location.Placement.Validate() == false {
		return nil, fmt.Errorf("invalid placement: %02X",
			int(adv.data.Location.Placement))
	}

	res := &EnvironmentalDevice{
		location:      adv.data.Location,
		assignedSince: adv.data.CurrentPoint.Timestamp.ToTime(),

		Location:    BuildSensorLocation(adv.data.Location),
		Address:     adv.address.String(),
		LastSeen:    adv.receivedAt,
		Battery:     setFloat(adv.data.Battery.Float64()),
		MemoryUsage: setFloat(adv.data.Memory.Float64()),
		Current: EnvironmentalState{
			Timestamp:        adv.data.CurrentPoint.Timestamp.ToTime(),
			Temperature_C:    setFloat(adv.data.CurrentPoint.Temperature.Float64()),
			Humidity_percent: setFloat(adv.data.CurrentPoint.Humidity.Float64()),
			Pressure_hPa:     setFloat(adv.data.CurrentPoint.Pressure.Float64()),
			CO2_PPM:          setCO2(adv.data.CurrentPoint.CO2),
		},
	}

	res.TimeOffset = adv.receivedAt.Sub(res.Current.Timestamp)
	return res, nil
}

func (d *EnvironmentalDevice) updateData(adv EnvironmentalAdvertisment) EnvironmentalReading {
	if d.location != adv.data.Location {
		d.location = adv.data.Location
		d.Location = BuildSensorLocation(adv.data.Location)
		d.assignedSince = adv.data.CurrentPoint.Timestamp.ToTime()
	}

	reading := EnvironmentalReading{
		LocationID: d.Location.LocationID,
		SensorID:   d.Address,
		ReceivedAt: adv.receivedAt,
	}

	d.Battery = setFloat(adv.data.Battery.Float64())
	d.MemoryUsage = setFloat(adv.data.Memory.Float64())
	d.Current.Timestamp = adv.data.CurrentPoint.Timestamp.ToTime()
	d.TimeOffset = adv.receivedAt.Sub(d.Current.Timestamp)

	reading.setData(adv.data.CurrentPoint)

	updateField := func(value float64, stateValue **float64) {
		if math.IsNaN(value) {
			return
		}
		*stateValue = newValue(value)
	}
	updateField(
		adv.data.CurrentPoint.Temperature.Float64(),
		&d.Current.Temperature_C,
	)

	updateField(
		adv.data.CurrentPoint.Humidity.Float64(),
		&d.Current.Humidity_percent,
	)

	updateField(
		adv.data.CurrentPoint.Pressure.Float64(),
		&d.Current.Pressure_hPa,
	)

	if adv.data.CurrentPoint.CO2 != arisble.CO2ConcentrationNaN {
		d.Current.CO2_PPM = newValue(uint(adv.data.CurrentPoint.CO2))
	}

	return reading
}

func (d *EnvironmentalDevice) clone() EnvironmentalDevice {
	return *d
}
