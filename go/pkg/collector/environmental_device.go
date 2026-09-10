package collector

import (
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
	Temperature_C    float64   `json:"temperature_C"`
	Humidity_percent float64   `json:"humidity_percent"`
	Pressure_hPa     float64   `json:"pressure_hPa"`
	CO2_PPM          float64   `json:"co2_ppm"`
}

type EnvironmentalDevice struct {
	location           arisble.Location
	assigned_since     time.Time
	Location           SensorLocation     `json:"location"`
	Address            string             `json:"address"`
	TimeOffset         time.Duration      `json:"time_offset_s"`
	LastSeen           time.Time          `json:"last_seen"`
	AdvertismentPeriod time.Duration      `json:"advertisment_period"`
	Battery            float64            `json:"battery"`
	MemoryUsage        float64            `json:"memory_usage"`
	Current            EnvironmentalState `json:"current_state"`
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
		location:       location,
		assigned_since: assignement.InstalledAt,
		Location:       BuildSensorLocation(location),
		Address:        assignement.SensorID,
		Battery:        math.NaN(),
		MemoryUsage:    math.NaN(),
		Current: EnvironmentalState{
			Temperature_C:    math.NaN(),
			Humidity_percent: math.NaN(),
			Pressure_hPa:     math.NaN(),
			CO2_PPM:          math.NaN(),
		},
	}, nil
}

func NewEnvironmentalDevice(adv EnvironmentalAdvertisment) (*EnvironmentalDevice, error) {
	if adv.data.Location.Placement.Validate() == false {
		return nil, fmt.Errorf("invalid placement: %02X",
			int(adv.data.Location.Placement))
	}

	res := &EnvironmentalDevice{
		location:       adv.data.Location,
		assigned_since: adv.data.CurrentPoint.Timestamp.ToTime(),

		Location:    BuildSensorLocation(adv.data.Location),
		Address:     adv.address.String(),
		LastSeen:    adv.receivedAt,
		Battery:     adv.data.Battery.Float64(),
		MemoryUsage: adv.data.Memory.Float64(),
		Current: EnvironmentalState{
			Timestamp:        adv.data.CurrentPoint.Timestamp.ToTime(),
			Temperature_C:    adv.data.CurrentPoint.Temperature.Float64(),
			Humidity_percent: adv.data.CurrentPoint.Humidity.Float64(),
			Pressure_hPa:     adv.data.CurrentPoint.Pressure.Float64(),
			CO2_PPM:          adv.data.CurrentPoint.CO2.Float64(),
		},
	}

	res.TimeOffset = adv.receivedAt.Sub(res.Current.Timestamp)
	return res, nil
}

func (d *EnvironmentalDevice) updateData(adv EnvironmentalAdvertisment) EnvironmentalReading {
	if d.location != adv.data.Location {
		d.location = adv.data.Location
		d.Location = BuildSensorLocation(adv.data.Location)
		d.assigned_since = adv.data.CurrentPoint.Timestamp.ToTime()
	}

	reading := EnvironmentalReading{
		LocationID: d.Location.LocationID,
		SensorID:   d.Address,
		ReceivedAt: adv.receivedAt,
	}

	d.Battery = adv.data.Battery.Float64()
	d.MemoryUsage = adv.data.Memory.Float64()
	d.Current.Timestamp = adv.data.CurrentPoint.Timestamp.ToTime()
	d.TimeOffset = adv.receivedAt.Sub(d.Current.Timestamp)

	reading.setData(adv.data.CurrentPoint)

	updateField := func(value float64, stateValue *float64) {
		if math.IsNaN(value) {
			return
		}
		*stateValue = value
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

	if adv.data.CurrentPoint.CO2 == arisble.CO2ConcentrationNaN {
		return reading
	}
	d.Current.CO2_PPM = adv.data.CurrentPoint.CO2.Float64()

	return reading
}

func (d *EnvironmentalDevice) clone() EnvironmentalDevice {
	return *d
}
