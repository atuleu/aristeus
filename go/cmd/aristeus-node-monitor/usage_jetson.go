package main

import (
	"context"
	"errors"
	"fmt"
	"log/slog"
	"os"
	"path/filepath"
	"strconv"
	"strings"
	"time"

	"github.com/prometheus/client_golang/prometheus"
)

var (
	jetsonGPUUsage = prometheus.NewGauge(prometheus.GaugeOpts{
		Name: "jetson_gpu_usage_percent",
		Help: "GPU usage in percent",
	})
	jetsonGPUFrequency = prometheus.NewGauge(prometheus.GaugeOpts{
		Name: "jetson_gpu_frequency_mhz",
		Help: "GPU frequency in MHz",
	})

	jetsonGPUTemp = prometheus.NewGauge(prometheus.GaugeOpts{
		Name: "jetson_gpu_temperature_celsius",
		Help: "Jetson GPU temperature in celsius",
	})
)

func jestonUsageLoop(ctx context.Context, reg *prometheus.Registry) error {
	logger := slog.With(slog.String("module", "jetson"))

	collectors := []prometheus.Collector{
		cpuCoreUsage,
		cpuUsage,
		cpuTemperature,
		memoryUsed,
		swapUsed,
		jetsonGPUUsage,
		jetsonGPUTemp,
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
			if err := jetsonPollUsage(logger); err != nil {
				return err
			}
		}
	}
}

func readGPULoadAndFrequency(path string) (float64, float64, error) {
	loadPath := filepath.Join(path, "load")
	freqPath := filepath.Join(path, "cur_freq")

	loadStr, err := os.ReadFile(loadPath)
	if err != nil {
		return 0.0, 0.0, err
	}
	freqStr, err := os.ReadFile(freqPath)
	if err != nil {
		return 0.0, 0.0, err
	}

	load, err := strconv.ParseUint(strings.TrimSpace(string(loadStr)), 10, 64)
	if err != nil {
		return 0.0, 0.0, fmt.Errorf("could not parse '%s': %w", loadPath, err)
	}
	freq, err := strconv.ParseUint(strings.TrimSpace(string(freqStr)), 10, 64)
	if err != nil {
		return 0.0, 0.0, fmt.Errorf("could not parse '%s': %w", freqPath, err)
	}

	return float64(load), float64(freq) / 1000000.0, nil
}

func pollGPUUsage() error {
	paths := []string{
		"/sys/class/devfreq/17000000.gpu",
		"/sys/class/devfreq/57000000.gpu/device",
	}
	for _, p := range paths {
		load, freq, err := readGPULoadAndFrequency(p)
		if errors.Is(err, os.ErrNotExist) {
			continue
		}
		if err != nil {
			return err
		}
		jetsonGPUUsage.Set(load)
		jetsonGPUFrequency.Set(freq)
		return nil
	}

	return errors.New("could not read GPU stats")
}

func jetsonPollUsage(logger *slog.Logger) error {
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

	tempGPU, err := readDeviceTemperature("thermal_zone1", "gpu-thermal")
	if err != nil {
		return err
	}
	jetsonGPUTemp.Set(tempGPU)
	logger.Debug("polled temperature",
		slog.Float64("cpu_temperature", tempCPU),
		slog.Float64("gpu_temperature", tempGPU))

	return nil
}
