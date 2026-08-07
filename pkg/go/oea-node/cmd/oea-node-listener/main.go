package main

import (
	"context"
	"fmt"
	"log/slog"
	"os"
	"os/signal"

	"github.com/go-ble/ble"
	ble_linux "github.com/go-ble/ble/linux"
)

func ErrAttr(err error) slog.Attr {
	return slog.Any("error", err)
}

func main() {
	if err := execute(); err != nil {
		slog.Error("unhandled error", ErrAttr(err))
		os.Exit(1)
	}
}

func onAdvertisement(adv ble.Advertisement) {
	slog.Info("advertisement", slog.String("address", adv.Addr().String()), slog.String("name", adv.LocalName()), slog.Any("manufacturer data", adv.ManufacturerData()))
}

func advertisementFilter(adv ble.Advertisement) bool {
	mdata := adv.ManufacturerData()
	if len(mdata) < 2 {
		return false
	}
	return mdata[0] == 0xff && mdata[1] == 0xff
}

func execute() error {

	dev, err := ble_linux.NewDevice()
	if err != nil {
		return fmt.Errorf("could not access BLE device: %w", err)
	}
	ble.SetDefaultDevice(dev)

	ctx, stop := signal.NotifyContext(context.Background(), os.Interrupt)
	defer stop()

	err = ble.Scan(ctx, false, onAdvertisement, advertisementFilter)

	if err != nil && err != context.Canceled {
		return fmt.Errorf("scan failure: %w", err)
	}

	return nil
}
