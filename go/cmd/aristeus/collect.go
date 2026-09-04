package main

import (
	"context"
	"errors"
	"os"
	"os/signal"
)

type CollectCommand struct {
}

func (c *CollectCommand) Execute(args []string) error {
	collector, err := NewCollector()
	if err != nil {
		return err
	}

	ctx, stop := signal.NotifyContext(context.Background(), os.Interrupt)
	defer stop()

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
