package main

import (
	"bufio"
	"fmt"
	"io"
	"log/slog"
	"math"
	"os"
	"path/filepath"
	"strconv"
	"strings"

	"github.com/prometheus/client_golang/prometheus"
)

var (
	cpuCoreUsage = prometheus.NewGaugeVec(prometheus.GaugeOpts{
		Name: "cpu_core_usage_percent",
		Help: "Usage percentage per CPU core",
	}, []string{"core"})

	cpuUsage = prometheus.NewGauge(prometheus.GaugeOpts{
		Name: "cpu_total_usage_percent",
		Help: "Total CPU usage",
	})

	cpuTemperature = prometheus.NewGauge(prometheus.GaugeOpts{
		Name: "cpu_temperature_celsius",
		Help: "CPU temperature in celsius",
	})

	memoryUsed = prometheus.NewGauge(prometheus.GaugeOpts{
		Name: "memory_usage_bytes",
		Help: "memory used in bytes",
	})

	swapUsed = prometheus.NewGauge(prometheus.GaugeOpts{
		Name: "memory_swap_total_bytes",
		Help: "total swap memory in bytes",
	})

	socTemperature = prometheus.NewGauge(prometheus.GaugeOpts{
		Name: "cpu_temperature_celsius",
		Help: "CPU temperature in celsius",
	})
)

type CpuStats struct {
	Idle  uint64
	Total uint64
}

func (s CpuStats) Usage_percent() float64 {
	if s.Total == 0 {
		return math.NaN()
	}
	return 100.0 * (1.0 - float64(s.Idle)/float64(s.Total))
}

func parseProcLine(line string) (label string, stats CpuStats, err error) {
	fields := strings.Fields(line)

	for i, s := range fields[1:] {
		v, err := strconv.ParseUint(s, 10, 64)
		if err != nil {
			return "", CpuStats{}, fmt.Errorf("could not parse line '%s': %w", line, err)
		}
		stats.Total += v
		if i == 3 || i == 4 {
			stats.Idle += v
		}
	}
	return fields[0], stats, nil
}

func parseProcStats(r io.Reader) (total CpuStats, cores map[string]CpuStats, err error) {
	scanner := bufio.NewScanner(r)
	cores = make(map[string]CpuStats)
	for scanner.Scan() {

		line := scanner.Text()

		if strings.HasPrefix(line, "cpu") == false {
			return total, cores, nil
		}
		label, stats, err := parseProcLine(line)
		if err != nil {
			return CpuStats{}, nil, err
		}

		if label == "cpu" {
			total = stats
		} else {
			cores["core"+strings.TrimPrefix(label, "cpu")] = stats
		}
	}
	return total, cores, scanner.Err()
}

func readProcStats() (CpuStats, map[string]CpuStats, error) {
	file, err := os.Open("/proc/stat")
	if err != nil {
		return CpuStats{}, nil, err
	}
	defer file.Close()

	return parseProcStats(file)
}

func pollCPUUsage(logger *slog.Logger) error {
	cpu, cores, err := readProcStats()
	if err != nil {
		return fmt.Errorf("could not read '/proc/stat': %w", err)
	}

	if cpu.Total != 0 {
		cpuUsage.Set(cpu.Usage_percent())
	}
	for label, stat := range cores {
		if stat.Total != 0 {
			cpuCoreUsage.WithLabelValues(label).Set(stat.Usage_percent())
		}
	}

	logger.Debug("polled /proc/stat",
		slog.Any("cpu", cpu),
		slog.Any("cores", cores))

	return nil
}

type MemoryStats struct {
	MemTotalBytes     uint64
	MemAvailableBytes uint64
	SwapTotalBytes    uint64
	SwapFreeBytes     uint64
}

func (s MemoryStats) MemoryUsed_percent() float64 {
	if s.MemTotalBytes == 0 {
		return math.NaN()
	}
	return 100.0 * (1.0 - float64(s.MemAvailableBytes)/float64(s.MemTotalBytes))
}

func parseProcMeminfo(r io.Reader) (MemoryStats, error) {
	scanner := bufio.NewScanner(r)
	values := make(map[string]uint64)
	toParse := map[string]bool{
		"MemTotal": true, "MemAvailable": true, "SwapTotal": true, "SwapFree": true,
	}
	for scanner.Scan() {
		line := scanner.Text()
		fields := strings.Fields(line)
		key := strings.TrimSuffix(fields[0], ":")
		val, err := strconv.ParseUint(fields[1], 10, 64)
		if err != nil {
			return MemoryStats{}, fmt.Errorf("could not parse '%s': %w", line, err)
		}
		values[key] = val
		delete(toParse, key)
		if len(toParse) == 0 {
			break
		}
	}
	return MemoryStats{
		MemTotalBytes:     values["MemTotal"] * 1024,
		MemAvailableBytes: values["MemAvailable"] * 1024,
		SwapTotalBytes:    values["SwapTotal"] * 1024,
		SwapFreeBytes:     values["SwapFree"] * 1024,
	}, scanner.Err()
}

func pollMemoryUsage(logger *slog.Logger) error {
	file, err := os.Open("/proc/meminfo")
	if err != nil {
		return err
	}
	defer file.Close()
	stats, err := parseProcMeminfo(file)
	if err != nil {
		return fmt.Errorf("could not parse '/proc/meminfo': %w", err)
	}
	if stats.MemTotalBytes != 0 {
		memoryUsed.Set(float64(stats.MemTotalBytes - stats.MemAvailableBytes))
	}

	if stats.SwapTotalBytes != 0 {
		swapUsed.Set(float64(stats.SwapTotalBytes - stats.SwapFreeBytes))
	}
	logger.Debug("polled /proc/meminfo",
		slog.Any("memory_stats", stats))
	return nil
}

func readZoneTemperature(zone string) (string, float64, error) {
	dir := filepath.Join("/sys/class/thermal", zone)
	name, err := os.ReadFile(filepath.Join(dir, "type"))
	if err != nil {
		return "", 0.0, err
	}
	tempStr, err := os.ReadFile(filepath.Join(dir, "temp"))
	if err != nil {
		return "", 0.0, err
	}
	value, err := strconv.ParseUint(strings.TrimSpace(string(tempStr)), 10, 64)
	if err != nil {
		return "", 0.0, err
	}
	return strings.TrimSpace(string(name)), float64(value) / 1000.0, nil
}

func readDeviceTemperature(zone, expected string) (float64, error) {
	label, value, err := readZoneTemperature(zone)
	if err != nil {
		return 0.0, fmt.Errorf("could not read info for '%s': %w", zone, err)
	}
	if label != expected {
		return 0.0, fmt.Errorf("unexpected label %s for '%s', expected: %s", label, zone, expected)
	}
	return value, nil
}
