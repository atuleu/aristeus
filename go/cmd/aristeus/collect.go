package main

import (
	"context"
	"errors"
	"os"
	"os/signal"

	"github.com/atuleu/aristeus/go/pkg/collector"
)

type CollectCommand struct {
	HiveIDs []uint8 `short:"i" long:"hive-id" description:"hive id to filter, none accepts all"`
}

func (c *CollectCommand) Execute(args []string) error {

	ctx, stop := signal.NotifyContext(context.Background(), os.Interrupt)
	defer stop()

	collector, err := collector.NewCollector(c.HiveIDs, nil, nil)
	if err != nil {
		return err
	}

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
