package main

import (
	"context"
	"fmt"
	"log/slog"
	"time"

	"github.com/atuleu/aristeus/go/pkg/arisble"
	"github.com/go-ble/ble"
	ble_linux "github.com/go-ble/ble/linux"
)

type JournalOptions struct {
	Since        string `long:"since" description:"sets a minimum time, in RFC3339 format"`
	Until        string `long:"until" description:"sets a maximum time, in RFC3339 format"`
	since, until arisble.Timestamp
}

type DownloadCommand struct {
	Voltage bool `short:"V" long:"voltage" description:"parses voltage instead of pressure"`
	Args    struct {
		Address string `positional-args-name:"ADDRESS" required:"yes"`
	} `positional-args:"yes"`
}

type EraseCommand struct {
	Args struct {
		Address string `positional-args-name:"ADDRESS" required:"yes"`
	} `positional-args:"yes"`
}

var journalOptions = &JournalOptions{}

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

func (opts *JournalOptions) finishParse() error {
	if len(opts.Since) > 0 {
		since, err := time.Parse(time.RFC3339, opts.Since)
		if err != nil {
			return fmt.Errorf("could not parse since time `%s`: %w", opts.Since, err)
		}
		opts.since = arisble.NewTimestamp(since)
	} else {
		opts.since = arisble.TimestampNaN
	}

	if len(opts.Until) > 0 {
		until, err := time.Parse(time.RFC3339, opts.Until)
		if err != nil {
			return fmt.Errorf("could not parse until time `%s`: %w", opts.Until, err)
		}
		opts.until = arisble.NewTimestamp(until)
	} else {
		opts.until = arisble.TimestampNaN
	}
	return nil
}

func (c *DownloadCommand) Execute(args []string) error {
	err := journalOptions.finishParse()
	if err != nil {
		return err
	}

	dev, err := ble_linux.NewDevice()
	if err != nil {
		return err
	}

	logger := slog.With(slog.String("address", c.Args.Address))

	logger.Info("connecting")
	ctx, cancel := context.WithTimeout(context.Background(), 15*time.Second)
	defer cancel()
	conn, err := arisble.NewBLEDeviceConn(dev, ctx, ble.NewAddr(c.Args.Address))
	if err != nil {
		return fmt.Errorf("could not connect to device: %w", err)
	}
	defer conn.Close()
	// Increase timeout for longer operation
	ctxLong, cancelLong := context.WithTimeout(context.Background(), 5*time.Minute)
	defer cancelLong()
	conn.SetContext(ctxLong)

	results, err := conn.ReportRecords(journalOptions.since, journalOptions.until)
	if err != nil {
		return fmt.Errorf("could not report records: %w", err)
	}

	if c.Voltage == true {
		fmt.Printf("#timestamp,temperature(°C),humidity(%%),voltage_loaded(mV),voltage_open(mV),CO2(PPM)\n")
	} else {
		fmt.Printf("#timestamp,temperature(°C),humidity(%%),pressure(hPa),CO2(PPM)\n")
	}

	for dp := range results {
		if dp.Error != nil {
			slog.Error("got readout error", slog.String("error", dp.Error.Error()))
			continue
		}
		if c.Voltage == true {
			loaded, open := dp.Value.Pressure.ToVoltage()
			fmt.Printf("%s,%.2f,%.1f,%d,%d,%d\n",
				dp.Value.Timestamp.ToTime().Format(time.RFC3339),
				dp.Value.Temperature.Float64(),
				dp.Value.Humidity.Float64(),
				loaded,
				open,
				dp.Value.CO2)

		} else {
			fmt.Printf("%s,%.2f,%.1f,%.3f,%d\n",
				dp.Value.Timestamp.ToTime().Format(time.RFC3339),
				dp.Value.Temperature.Float64(),
				dp.Value.Humidity.Float64(),
				dp.Value.Pressure.Float64(),
				dp.Value.CO2)
		}

	}

	return nil
}

func (c *EraseCommand) Execute(args []string) error {
	err := journalOptions.finishParse()
	if err != nil {
		return err
	}

	dev, err := ble_linux.NewDevice()
	if err != nil {
		return err
	}

	logger := slog.With(slog.String("address", c.Args.Address))

	logger.Info("connecting")
	ctx, cancel := context.WithTimeout(context.Background(), 15*time.Second)
	defer cancel()
	conn, err := arisble.NewBLEDeviceConn(dev, ctx, ble.NewAddr(c.Args.Address))
	if err != nil {
		return fmt.Errorf("could not connect to device: %w", err)
	}
	defer conn.Close()

	// Increase timeout for longer operation
	ctxLong, cancelLong := context.WithTimeout(context.Background(), 5*time.Minute)
	defer cancelLong()
	conn.SetContext(ctxLong)

	errors, err := conn.DeleteRecords(journalOptions.since, journalOptions.until)
	if err != nil {
		return fmt.Errorf("could not report records: %w", err)
	}

	hadError := false
	for err := range errors {
		logger.Error("got error during deletion", slog.String("error", err.Error()))
		hadError = true
	}

	if hadError == false {
		logger.Info("deleted records",
			slog.String("since", journalOptions.Since),
			slog.String("until", journalOptions.Until))
	}
	return nil
}

func init() {
	journalCommand, err := parser.AddCommand("journal", "operation on journal of BLE device", "collection of operation for fjournal data", journalOptions)
	if err != nil {
		panic(err)
	}

	_, err = journalCommand.AddCommand("download", "download journal data from", "download journal data from a device.", &DownloadCommand{})
	if err != nil {
		panic(err)
	}

	_, err = journalCommand.AddCommand("erase", "erase journal data from", "erase journal data from a device.", &EraseCommand{})
	if err != nil {
		panic(err)
	}

}
