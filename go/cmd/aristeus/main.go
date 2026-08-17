package main

import (
	"context"
	"fmt"
	"log/slog"
	"os"
	"sync"
	"time"

	"github.com/atuleu/aristeus/go/pkg/arisble"
	"github.com/go-ble/ble"
	ble_linux "github.com/go-ble/ble/linux"
	"github.com/jessevdk/go-flags"
)

type Options struct {
	Address string `short:"A" long:"address" description:"address to download from"`
}

func main() {
	if err := execute(); err != nil {
		slog.Error("Unhandled error", slog.String("error", err.Error()))
		os.Exit(1)
	}
}

func findCharacteristic(profile *ble.Profile, serviceUUID, charUUID ble.UUID) (*ble.Characteristic, error) {
	for _, service := range profile.Services {
		if service.UUID.Equal(serviceUUID) {
			for _, char := range service.Characteristics {
				if char.UUID.Equal(charUUID) {
					return char, nil
				}
			}
		}
	}
	return nil, fmt.Errorf("Not found")
}

func execute() error {
	var opts Options
	if _, err := flags.Parse(&opts); err != nil {
		if flags.WroteHelp(err) == true {
			return nil
		}
		return err
	}

	dev, err := ble_linux.NewDevice()
	if err != nil {
		return err
	}

	logger := slog.With(slog.String("address", opts.Address))

	logger.Info("connecting")
	conn, err := dev.Dial(context.Background(), ble.NewAddr(opts.Address))
	if err != nil {
		return fmt.Errorf("could not connect to '%s': %w", opts.Address, err)
	}
	defer conn.Conn().Close()

	slog.Info("fetching profile")
	profile, err := conn.DiscoverProfile(false)
	if err != nil {
		return fmt.Errorf("could not discover profile: %w", err)
	}

	streamChar, err := findCharacteristic(profile, arisble.CustomServiceUUID, arisble.StreamDataUUID)
	if err != nil {
		return fmt.Errorf("could not find stream data characteristic on device")
	}
	racpChar, err := findCharacteristic(profile, arisble.CustomServiceUUID, arisble.RACPUUID)
	if racpChar == nil {
		return fmt.Errorf("could not find RACP characteristic on device")
	}

	mx := sync.Mutex{}
	var points *chan arisble.DataPoint
	var errors *chan error
	*points = make(chan arisble.DataPoint, 10)
	*errors = make(chan error, 10)

	onData := func(data []byte) {
		var dp arisble.DataPoint
		err := dp.UnmarshalBinary(data)
		mx.Lock()
		defer mx.Unlock()

		if err != nil {
			if errors != nil {
				*errors <- err
			}
		} else {
			if points != nil {
				*points <- dp
			}
		}
	}

	onRACP := func(data []byte) {
		defer func() {
			mx.Lock()
			close(*points)
			close(*errors)
			points = nil
			errors = nil
			mx.Unlock()
		}()
		if len(data) != 4 {
			*errors <- fmt.Errorf("unexpected GATT server response %x, length should be 4", data)
		}
		if data[0] != 0x06 || data[1] != 0x00 {
			*errors <- fmt.Errorf("unexpected GATT response %x: not a RSP opcode", data)
		}

		if data[2] != 0x01 {
			*errors <- fmt.Errorf("unexpected RACP response code %x, expected report records", data[2])
		}

		if data[3] != 0x01 {
			*errors <- fmt.Errorf("unexpected RACP response code %x", data[3])
		}
	}

	conn.Subscribe(streamChar, false, onData)
	conn.Subscribe(racpChar, true, onRACP)
	conn.WriteCharacteristic(racpChar, []byte{0x01, 0x01}, false)
	fmt.Printf("#timestamp,voltage_open(mV), voltage_loaded(mV)\n")
	var wg sync.WaitGroup

	wg.Go(func() {
		for err := range *errors {
			slog.Error("got RACP error", slog.String("error", err.Error()))
		}
	})

	wg.Go(func() {
		for dp := range *points {
			loaded, open := dp.Pressure.ToVoltage()
			fmt.Printf("%s,%d,%d\n", dp.Timestamp.ToTime().Format(time.RFC3339), loaded, open)
		}
	})

	wg.Wait()
	return nil
}
