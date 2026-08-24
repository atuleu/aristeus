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

type DownloadCommand struct {
	Voltage      bool   `short:"V" long:"voltage" description:"parses voltage instead of pressure"`
	Since        string `long:"since" description:"sets a minimum time, in RFC3339 format"`
	Until        string `long:"until" description:"sets a maximum time, in RFC3339 format"`
	since, until arisble.Timestamp
	Args         struct {
		Address string `positional-args-name:"ADDRESS" required:"yes"`
	} `positional-args:"yes"`
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

func (c *DownloadCommand) finishParse() error {
	if len(c.Since) > 0 {
		since, err := time.Parse(time.RFC3339, c.Since)
		if err != nil {
			return fmt.Errorf("could not parse since time `%s`: %w", c.Since, err)
		}
		c.since = arisble.NewTimestamp(since)
	}

	if len(c.Until) > 0 {
		until, err := time.Parse(time.RFC3339, c.Until)
		if err != nil {
			return fmt.Errorf("could not parse until time `%s`: %w", c.Until, err)
		}
		c.until = arisble.NewTimestamp(until)
	}
	return nil
}

func (c *DownloadCommand) Execute(args []string) error {
	err := c.finishParse()
	if err != nil {
		return err
	}

	dev, err := ble_linux.NewDevice()
	if err != nil {
		return err
	}

	logger := slog.With(slog.String("address", c.Args.Address))

	logger.Info("connecting")

	conn, err := arisble.NewBLEDeviceConn(dev, context.Background(), ble.NewAddr(c.Args.Address))
	if err != nil {
		return fmt.Errorf("could not connect to device: %w", err)
	}
	defer conn.Close()

	results, err := conn.ReportRecords(c.since, c.until)
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

func init() {
	_, err := parser.AddCommand("download", "download backload data from a device", "download journal data from a device.", &DownloadCommand{})
	if err != nil {
		panic(err)
	}
}
