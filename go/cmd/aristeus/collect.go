package main

import (
	"context"
	"errors"
	"fmt"
	"os"
	"os/signal"

	ble_linux "github.com/go-ble/ble/linux"
	"github.com/atuleu/aristeus/go/pkg/collector"
)

type CollectCommand struct {
	HiveIDs []uint8 `short:"i" long:"hive-id" description:"hive id to filter, none accepts all"`
}

func (c *CollectCommand) Execute(args []string) error {
	collector, err := collector.NewCollector(c.HiveIDs, nil)
	if err != nil {
		return err
	}

	ctx, stop := signal.NotifyContext(context.Background(), os.Interrupt)
	defer stop()

	dev, err := ble_linux.NewDevice()
	if err != nil {
		return fmt.Errorf("could no open BLE device: %w", err)
	}

	err = collector.Collect(ctx, dev)

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
