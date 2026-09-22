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
	Humidity                   uint8
	ReadoutID                  uint16
	Temperature, TemperatureRT float64
	Weight                     [4]float64
	StatusWeight               uint16
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
	d.Humidity = data[16]
	d.ReadoutID = binary.LittleEndian.Uint16(data[7:9])
	d.Weight[0] = parseWeight(data[12:14])
	d.Weight[1] = parseWeight(data[14:16])
	d.Weight[2] = parseWeight(data[17:19])
	d.Weight[3] = parseWeight(data[19:21])

	d.TemperatureRT = (float64((uint16(data[11])<<8)|uint16(data[5])) - 5000.0) / 100.0
	d.Temperature = (float64(binary.BigEndian.Uint16(data[9:11])) - 5000.0) / 100.0
	d.StatusWeight = binary.LittleEndian.Uint16(data[21:23])

	return nil
}

type ScaleAdvertisment struct {
	address    ble.Addr
	data       ScaleData
	receivedAt time.Time
}
