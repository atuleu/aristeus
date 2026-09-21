package main

import (
	"context"
	"errors"
	"log/slog"
	"os"
	"os/signal"
	"path/filepath"

	"github.com/adrg/xdg"
	"github.com/atuleu/aristeus/go/pkg/collector"
)

type CollectCommand struct {
	HiveIDs                []uint8          `short:"i" long:"hive-id" description:"hive id to filter, none accepts all"`
	Scales                 map[string]uint8 `long:"scale" description:"maps a scale address to an id"`
	DisableSynchronization bool             `long:"disable-synchronization" description:"disable device synchronization"`
}

func (c *CollectCommand) Execute(args []string) (err error) {

	ctx, stop := signal.NotifyContext(context.Background(), os.Interrupt)
	defer stop()

	subpath := "io.github.atuleu.aristeus/config.yml"
	paths := make([]string, 0, len(xdg.ConfigDirs)+1)
	for _, d := range xdg.ConfigDirs {
		paths = append(paths, filepath.Join(d, subpath))
	}
	paths = append(paths, filepath.Join(xdg.ConfigHome, subpath))
	slog.Debug("using config path",
		slog.Any("paths", paths))

	config, err := collector.NewCollectorConfigFromFiles(paths...)
	if err != nil {
		return err
	}
	if len(c.HiveIDs) > 0 {
		config.ApplyOptions(collector.WithHiveIDFilter(c.HiveIDs...))
	}
	if c.DisableSynchronization {
		config.SynchronizeDevices = false
	}
	if len(c.Scales) > 0 {
		config.ApplyOptions(collector.WithScaleMapping(c.Scales))
	}

	collector, err := collector.NewCollector(config)
	if err != nil {
		return err
	}
	defer func() { err = errors.Join(err, collector.Close()) }()

	err = collector.Collect(ctx)

	if err != nil && errors.Is(err, context.Canceled) == false {
		return err
	}

	return nil
}

func init() {
	_, err := parser.AddCommand("collect", "Collect data from BLE node around the physical device", "Collects and synchronize time data from around the physical device", &CollectCommand{})
	if err != nil {
		panic(err.Error())
	}
}
