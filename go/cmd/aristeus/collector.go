package main

import (
	"context"
	"encoding/binary"
	"errors"
	"fmt"
	"log/slog"
	"sync"

	"github.com/atuleu/aristeus/go/pkg/arisble"
	"github.com/go-ble/ble"
	ble_linux "github.com/go-ble/ble/linux"
)

type Collector struct {
	mx      sync.RWMutex
	devices map[string]EnvironmentalDevice

	logger *slog.Logger
}

func (c *Collector) bleAdvHandler() ble.AdvHandler {
	filter := c.bleAdvFilter()
	return func(adv ble.Advertisement) {

		if filter(adv) == false {
			return
		}
		var data arisble.AdvertisementData
		if err := data.UnmarshalBinary(adv.ManufacturerData()[2:]); err != nil {
			c.logger.Error("could not parse advertisment data", slog.String("error", err.Error()))
			return
		}

		c.logger.Info("got advertisment",
			slog.String("address", adv.Addr().String()),
			slog.Time("timestamp", data.CurrentPoint.Timestamp.ToTime()),
			slog.String("location", data.Location.String()),
			slog.String("battery", data.Battery.String()),
			slog.String("memory", data.Memory.String()),
			slog.String("temperature", data.CurrentPoint.Temperature.String()),
			slog.String("humidity", data.CurrentPoint.Humidity.String()),
			slog.String("pressure", data.CurrentPoint.Pressure.String()),
			slog.String("co2", data.CurrentPoint.CO2.String()),
		)

	}
}

func (c *Collector) bleAdvFilter() ble.AdvFilter {
	return func(adv ble.Advertisement) bool {
		mdata := adv.ManufacturerData()
		if len(mdata) < 2 {
			return false
		}
		manufacturerID := binary.LittleEndian.Uint16(mdata[0:2])
		if len(mdata) == 20 && manufacturerID == 0xFFFF {
			return true
		}
		return false
	}
}

func (c *Collector) bleLoop(ctx context.Context, dev ble.Device, tasks <-chan func()) error {
	c.logger.Info("BLE control loop started")
	startScanning := func() (context.CancelFunc, <-chan error) {
		errors := make(chan error)
		scanContext, cancelContext := context.WithCancel(ctx)
		go func() {
			c.logger.Info("starting BLE scan")
			errors <- dev.Scan(scanContext, true, c.bleAdvHandler())
			close(errors)
		}()
		return cancelContext, errors
	}

	cancelScan, scanErrors := startScanning()
	for {
		select {
		case <-ctx.Done():
			c.logger.Info("collection done")
			return nil
		case err := <-scanErrors:
			if err != nil && errors.Is(err, context.Canceled) == false {
				c.logger.Error("scan error", slog.String("error", err.Error()))
				return err
			}
			startScanning()
		case task := <-tasks:
			cancelScan()
			err := <-scanErrors
			if err != nil && errors.Is(err, context.Canceled) == false {
				return err
			}
			task()
			cancelScan, scanErrors = startScanning()
		}

	}

}

func (c *Collector) Collect(ctx context.Context) error {
	dev, err := ble_linux.NewDevice()
	if err != nil {
		return fmt.Errorf("could not open BLE device: %w", err)
	}

	return c.bleLoop(ctx, dev, nil)

}

func NewCollector() (*Collector, error) {
	return &Collector{
		devices: make(map[string]EnvironmentalDevice),
		logger:  slog.With(slog.String("module", "collector")),
	}, nil

}
