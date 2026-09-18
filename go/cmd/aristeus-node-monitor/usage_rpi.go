package main

import (
	"context"
	"log/slog"
	"time"

	"github.com/prometheus/client_golang/prometheus"
)

func rpiUsageLoop(ctx context.Context, reg *prometheus.Registry) error {
	logger := slog.With(slog.String("module", "rpi_usage"))

	collectors := []prometheus.Collector{
		cpuCoreUsage,
		cpuUsage,
		cpuTemperature,
		memoryUsed,
		swapUsed,
	}
	for _, metric := range collectors {
		if err := reg.Register(metric); err != nil {
			return err
		}
	}

	ticker := time.NewTicker(opts.ScanPeriod)
	logger.Info("started", slog.Duration("period", opts.ScanPeriod))

	defer ticker.Stop()
	for {
		select {
		case <-ctx.Done():
			return nil
		case <-ticker.C:
			if err := rpiPollUsage(logger); err != nil {
				return err
			}
		}
	}
}

func rpiPollUsage(logger *slog.Logger) error {
	if err := pollCPUUsage(logger); err != nil {
		return err
	}

	if err := pollMemoryUsage(logger); err != nil {
		return err
	}

	tempCPU, err := readDeviceTemperature("thermal_zone0", "cpu-thermal")
	if err != nil {
		return err
	}
	cpuTemperature.Set(tempCPU)

	logger.Debug("polled temperature",
		slog.Float64("cpu_temperature", tempCPU),
	)

	return nil
}
