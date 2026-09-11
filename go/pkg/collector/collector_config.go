package collector

import (
	"context"
	"fmt"
	"os"
	"path"
	"path/filepath"
	"time"

	"github.com/adrg/xdg"
	"github.com/atuleu/aristeus/go/pkg/arisble"
)

type collectorDependencies struct {
	journal               DataJournal // for dependency injection
	scanner               BLEScanner
	cron                  CronScheduler
	clock                 Clock
	environmentalOperator environmentalOperator
}

func (c *collectorDependencies) doMissingInjection() error {
	var err error
	if c.journal == nil {
		c.journal, err = c.connectJournal()
		if err != nil {
			return err
		}
	}

	if c.clock == nil {
		c.clock = timeClock{}
	}

	if c.scanner == nil {
		device, err := arisble.NewBLEDevice()
		if err != nil {
			return fmt.Errorf("could not open BLE device: %w", err)
		}
		c.scanner = NewScanner(device)
	}

	if c.cron == nil {
		c.cron = NewCronScheduler(c.clock)
	}

	if c.environmentalOperator == nil {
		c.environmentalOperator = environmentalOperatorImpl{}
	}

	return nil
}

func (c *collectorDependencies) connectJournal() (DataJournal, error) {
	dbPath, err := xdg.DataFile(path.Join("io.github.atuleu.aristeus", "readings.db"))
	if err != nil {
		return nil, fmt.Errorf("could not generate datapath: %w", err)
	}
	err = os.MkdirAll(filepath.Dir(dbPath), 0755)
	if err != nil {
		return nil, fmt.Errorf("could not create journal directories: %w", err)
	}

	ctx, cancel := context.WithTimeout(context.Background(), 10*time.Second)
	defer cancel()
	journal, err := NewSQLiteStore(ctx, dbPath)
	if err != nil {
		return nil, fmt.Errorf("could not open journal `%s`: %w", dbPath, err)
	}
	return journal, nil
}

type CollectorConfig struct {
	deps *collectorDependencies

	HiveIDFilter               map[uint8]bool
	MinimumAssignementDuration time.Duration
	MaximalTimeOffset          time.Duration
	ActiveThresholdDuration    time.Duration
	ConnectionJitter           time.Duration
	JanitorTime                HourOfDay
	SynchronizeDevices         bool
}

type CollectorConfigOption func(*CollectorConfig)

func defaultCollectorConfig() CollectorConfig {
	return CollectorConfig{
		deps:                       new(collectorDependencies),
		HiveIDFilter:               nil,
		MinimumAssignementDuration: 5 * time.Minute,
		MaximalTimeOffset:          3 * time.Minute,
		ActiveThresholdDuration:    5 * time.Minute,
		ConnectionJitter:           10 * time.Minute,
		SynchronizeDevices:         true,
		JanitorTime:                HourOfDay{Hour: 1, Minute: 42},
	}
}

func WithSynchronizeDevice(enabled bool) CollectorConfigOption {
	return func(config *CollectorConfig) {
		config.SynchronizeDevices = enabled
	}
}

func WithHiveIDFilter(IDs ...uint8) CollectorConfigOption {
	return func(config *CollectorConfig) {
		config.HiveIDFilter = make(map[uint8]bool)
		for _, ID := range IDs {
			config.HiveIDFilter[ID] = true
		}
	}
}

func WithMinimumAssignementDuration(duration time.Duration) CollectorConfigOption {
	return func(config *CollectorConfig) {
		config.MinimumAssignementDuration = duration
	}
}

func WithMaximalTimeOffset(duration time.Duration) CollectorConfigOption {
	return func(config *CollectorConfig) {
		config.MaximalTimeOffset = duration
	}
}

func WithActiveThresholdDuration(duration time.Duration) CollectorConfigOption {
	return func(config *CollectorConfig) {
		config.ActiveThresholdDuration = duration
	}
}

func WithConnectionJitter(duration time.Duration) CollectorConfigOption {
	return func(config *CollectorConfig) {
		config.ConnectionJitter = duration
	}
}

func WithJanitorTime(hod HourOfDay) CollectorConfigOption {
	return func(config *CollectorConfig) {
		config.JanitorTime = hod
	}
}

func withJournal(journal DataJournal) CollectorConfigOption {
	return func(config *CollectorConfig) {
		config.deps.journal = journal
	}
}

func withScanner(scanner BLEScanner) CollectorConfigOption {
	return func(config *CollectorConfig) {
		config.deps.scanner = scanner
	}
}

func withCron(cron CronScheduler) CollectorConfigOption {
	return func(config *CollectorConfig) {
		config.deps.cron = cron
	}
}

func withClock(clock Clock) CollectorConfigOption {
	return func(config *CollectorConfig) {
		config.deps.clock = clock
	}
}

func withEnvironmentalOperator(eo environmentalOperator) CollectorConfigOption {
	return func(config *CollectorConfig) {
		config.deps.environmentalOperator = eo
	}
}

func NewCollectorConfig(opts ...CollectorConfigOption) CollectorConfig {
	cfg := defaultCollectorConfig()
	for _, opt := range opts {
		opt(&cfg)
	}
	return cfg
}
