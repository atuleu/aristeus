package collector

import (
	"context"
	"errors"
	"fmt"
	"os"
	"path"
	"path/filepath"
	"time"

	"github.com/adrg/xdg"
	"github.com/atuleu/aristeus/go/pkg/arisble"
	"go.yaml.in/yaml/v4"
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

type HiveID uint8
type HiveIDSet map[HiveID]bool

func (s *HiveIDSet) UnmarshalYAML(value *yaml.Node) error {
	var rawSlice []uint8
	if err := value.Decode(&rawSlice); err != nil {
		return err
	}

	if len(rawSlice) == 0 {
		*s = nil
		return nil
	}

	*s = make(HiveIDSet)
	for _, id := range rawSlice {
		(*s)[HiveID(id)] = true
	}
	return nil
}

type ScaleAddressMap map[string]HiveID

func (s *ScaleAddressMap) UnmarshalYAML(value *yaml.Node) error {
	type ScaleMapping struct {
		Address string `yaml:"address"`
		HiveID  uint8  `yaml:"hive-id"`
	}
	var rawMapping []ScaleMapping
	if err := value.Decode(&rawMapping); err != nil {
		return err
	}

	if len(rawMapping) == 0 {
		*s = nil
		return nil
	}
	res := make(ScaleAddressMap)
	ids := make(map[uint8]bool)
	for _, m := range rawMapping {
		if _, ok := res[m.Address]; ok == true {
			return fmt.Errorf("address `%s` is mapped multiple times", m.Address)
		}
		if ids[m.HiveID] != false {
			return fmt.Errorf("Hive ID %d is mapped multiple times", m.HiveID)
		}

		res[m.Address] = HiveID(m.HiveID)
		ids[m.HiveID] = true
	}
	*s = res
	return nil
}

type CollectorConfig struct {
	deps *collectorDependencies

	HiveIDFilter               HiveIDSet       `yaml:"hive-ids"`
	MinimumAssignementDuration time.Duration   `yaml:"minimum-assignment-duration"`
	MaximalTimeOffset          time.Duration   `yaml:"maximal-time-offset"`
	ActiveThresholdDuration    time.Duration   `yaml:"active-threshold-duration"`
	ConnectionJitter           time.Duration   `yaml:"connection-jitter"`
	JanitorTime                HourOfDay       `yaml:"janitor-schedule"`
	SynchronizeDevices         bool            `yaml:"synchronize-device"`
	ScaleAddresses             ScaleAddressMap `yaml:"scales"`
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
		config.HiveIDFilter = make(HiveIDSet)
		for _, ID := range IDs {
			config.HiveIDFilter[HiveID(ID)] = true
		}
	}
}

func WithScaleMapping(maps map[string]uint8) CollectorConfigOption {
	return func(config *CollectorConfig) {
		config.ScaleAddresses = ScaleAddressMap{}
		for addr, id := range maps {
			config.ScaleAddresses[addr] = HiveID(id)
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

func (c *CollectorConfig) ApplyOptions(opts ...CollectorConfigOption) {
	for _, opt := range opts {
		opt(c)
	}
}

func (c *CollectorConfig) LoadFromYAML(path string) error {
	file, err := os.Open(path)
	if err != nil {
		return err
	}
	defer file.Close()

	loader, err := yaml.NewLoader(file, yaml.WithV4Defaults())
	if err != nil {
		return err
	}
	return loader.Load(c)
}

func NewCollectorConfigFromFiles(paths ...string) (CollectorConfig, error) {
	cfg := defaultCollectorConfig()
	for _, p := range paths {
		err := cfg.LoadFromYAML(p)
		if err != nil && errors.Is(err, os.ErrNotExist) == false {
			return CollectorConfig{}, fmt.Errorf("could not read '%s': %w", p, err)
		}
	}

	return cfg, nil
}

func NewCollectorConfig(opts ...CollectorConfigOption) CollectorConfig {
	cfg := defaultCollectorConfig()
	cfg.ApplyOptions(opts...)
	return cfg
}
