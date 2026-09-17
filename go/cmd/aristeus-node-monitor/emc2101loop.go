package main

import (
	"context"
	"fmt"
	"log/slog"
	"time"

	"github.com/atuleu/aristeus/go/pkg/emc2101"
	"github.com/prometheus/client_golang/prometheus"
	"github.com/prometheus/client_golang/prometheus/promauto"
	"periph.io/x/conn/v3/i2c/i2creg"
	"periph.io/x/host/v3"
)

func emc2101Loop(ctx context.Context, reg *prometheus.Registry) error {
	logger := slog.With(slog.String("module", "I2C"), slog.String("bus", opts.I2CBus))
	state, err := host.Init()
	if err != nil {
		return fmt.Errorf("could not initialize periph.io: %w", err)
	}
	logger.Info("Loaded drivers", slog.Any("drivers", state))
	bus, err := i2creg.Open(opts.I2CBus)
	if err != nil {
		return fmt.Errorf("could not open the I2C bus '%s': %w", opts.I2CBus, err)
	}
	defer bus.Close()

	dev, err := emc2101.NewDevice(bus, emc2101.Config{})
	if err != nil {
		return err
	}

	table := make([]emc2101.LUTPoint, 0, len(opts.LUTPoint))
	for _, pt := range opts.LUTPoint {
		table = append(table, emc2101.LUTPoint(pt))
	}
	err = dev.SetLUT(table)
	if err != nil {
		return fmt.Errorf("could not set control LUT: %w", err)
	}

	temperature := promauto.With(reg).NewGauge(prometheus.GaugeOpts{
		Name: "enclosure_temperature_celcius",
		Help: "temperature inside the vision enclosure",
	})
	speed := promauto.With(reg).NewGauge(prometheus.GaugeOpts{
		Name: "enclosure_fan_speed_rpm",
		Help: "speed of the vision enclosure fan",
	})

	timer := time.NewTimer(opts.ScanPeriod)
	defer timer.Stop()
	logger.Info("started",
		slog.Duration("period", opts.ScanPeriod))

	for {
		select {
		case <-ctx.Done():
			return nil
		case t := <-timer.C:
			temp, err := dev.ReadTemperature()
			if err != nil {
				return fmt.Errorf("could not read temperature: %w", err)
			}
			rpm, err := dev.ReadRPM()
			if err != nil {
				return fmt.Errorf("could not read RPM: %w", err)
			}
			temperature.Set(temp)
			speed.Set(float64(rpm))
			logger.Debug("measurement",
				slog.Time("time", t),
				slog.Float64("temperature", temp),
				slog.Uint64("RPM", uint64(rpm)),
			)

		}
	}

}
