package collector

import (
	"encoding/binary"
	"errors"
	"fmt"
	"time"

	"github.com/go-ble/ble"
)

type ScaleData struct {
	Model                      uint8
	Version                    string
	Battery                    uint8
	Ellapsed                   uint16
	Temperature, TemperatureRT float64
	WeightReal                 float64
	WeightLeftFront            float64
	WeightLeftBack             float64
	WeightRightFront           float64
	WeightRightBack            float64
}

func parseWeight(data []byte) float64 {
	return (float64(binary.LittleEndian.Uint16(data)) - 32767) / 100.0
}

func (d *ScaleData) UnmarshalBinary(data []byte) error {
	if len(data) < 23 {
		return errors.New("insufficient size")
	}
	d.Model = data[2]
	d.Version = fmt.Sprintf("%d.%d", data[4], data[3])
	d.Battery = data[6]
	d.Ellapsed = binary.LittleEndian.Uint16(data[7:9])
	d.WeightReal = parseWeight(data[21:23])
	d.WeightLeftFront = parseWeight(data[12:14])
	d.WeightRightFront = parseWeight(data[14:16])
	d.WeightLeftBack = parseWeight(data[17:19])
	d.WeightRightBack = parseWeight(data[19:21])

	d.TemperatureRT = (float64((uint16(data[11])<<8)|uint16(data[5])) - 5000.0) / 100.0
	d.Temperature = (float64(binary.LittleEndian.Uint16(data[11:13])) - 5000.0) / 100.0

	return nil
}

type ScaleAdvertisment struct {
	address    ble.Addr
	data       ScaleData
	receivedAt time.Time
}
