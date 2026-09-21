package collector

import (
	"errors"
	"fmt"
	"time"

	"github.com/go-ble/ble"
)

type ScaleData struct {
	Model            uint8
	Version          string
	Battery          uint8
	Ellapsed         uint16
	Temperature      float64
	TotalWeight      float64
	WeightLeftFront  float64
	WeightLeftBack   float64
	WeightRightFront float64
	WeightRightBack  float64
}

func (d *ScaleData) UnmarshalBinary(data []byte) error {
	if len(data) < 22 {
		return errors.New("insufficient size")
	}
	d.Model = data[2]
	d.Version = fmt.Sprintf("%d.%d", data[4], data[3])
	d.Battery = data[6]

	return nil
}

type ScaleAdvertisment struct {
	address    ble.Addr
	data       ScaleData
	receivedAt time.Time
}
