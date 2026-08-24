package main

import (
	"context"
	"fmt"
	"time"

	"github.com/atuleu/aristeus/go/pkg/arisble"
	"github.com/go-ble/ble"
)

type DataPoint struct {
	Timestamp   time.Time `json:"timestamp"`
	Temperature float64   `json:"temperature_C"`
	Humidity    float64   `json:"humidity_percent"`
	Pressure    float64   `json:"pressure_hPa"`
	CO2         uint      `json:"co2_ppm"`
}

type Device struct {
	Address      ble.Addr      `json:"address"`
	TimeOffset_s int           `json:"time_offset"`
	Interval     time.Duration `json:"interval_ms"`
	Current      DataPoint     `json:"measurement"`
	Memory       float64       `json:"memory_usage"`
	Battery      float64       `json:"battery"`
}

func (d *Device) synchronize(dev ble.Device, ctx context.Context) error {
	conn, err := arisble.NewBLEDeviceConn(dev, ctx, d.Address)
	if err != nil {
		return err
	}
	defer conn.Close()
	return conn.SynchronizeBLEDevice()
}

func (d *Device) dumpJournal(dev ble.Device, ctx context.Context, since, until *time.Time, erase bool) (<-chan arisble.RACPResult[DataPoint], error) {
	conn, err := arisble.NewBLEDeviceConn(dev, ctx, d.Address)
	if err != nil {
		return nil, err
	}
	start := arisble.TimestampNaN
	end := arisble.TimestampNaN
	if since != nil {
		start = arisble.NewTimestamp(*since)
	}
	if until != nil {
		end = arisble.NewTimestamp(*until)
	}

	points, err := conn.ReportRecords(start, end)
	if err != nil {
		return nil, err
	}

	result := make(chan arisble.RACPResult[DataPoint], 10)
	go func() {
		defer close(result)
		for dp := range points {
			if dp.Error != nil {
				result <- arisble.RACPResult[DataPoint]{Error: dp.Error}
			} else {
				result <- arisble.RACPResult[DataPoint]{Value: DataPoint{
					Timestamp:   dp.Value.Timestamp.ToTime(),
					Temperature: dp.Value.Temperature.Float64(),
					Humidity:    dp.Value.Humidity.Float64(),
					Pressure:    dp.Value.Pressure.Float64(),
					CO2:         uint(dp.Value.CO2),
				}}
			}
		}
		if erase == false {
			conn.Close()
			return
		}
		errors, err := conn.DeleteRecords(start, end)
		if err == nil {
			err = <-errors
		}
		result <- arisble.RACPResult[DataPoint]{Error: fmt.Errorf("could not delete record after reading: %w", err)}
		conn.Close()
	}()
	return result, nil
}
