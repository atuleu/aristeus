package main

import (
	"context"
	"fmt"
	"log/slog"
	"sync"
	"time"

	"github.com/atuleu/aristeus/go/pkg/arisble"
	"github.com/go-ble/ble"
	ble_linux "github.com/go-ble/ble/linux"
)

type DownloadCommand struct {
	Voltage      bool   `short:"V" long:"voltage" description:"parses voltage instead of pressure"`
	Since        string `long:"since" description:"sets a minimum time, in RFC3339 format"`
	Until        string `long:"until" description:"sets a maximum time, in RFC3339 format"`
	since, until *time.Time
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
	var err error
	if len(c.Since) > 0 {
		c.since = &time.Time{}

		*c.since, err = time.Parse(time.RFC3339, c.Since)
		if err != nil {
			return fmt.Errorf("could not parse since time `%s`: %w", c.Since, err)
		}
	}

	if len(c.Until) > 0 {
		c.until = &time.Time{}
		*c.until, err = time.Parse(time.RFC3339, c.Until)
		if err != nil {
			return fmt.Errorf("could not parse until time `%s`: %w", c.Until, err)
		}
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
	conn, err := dev.Dial(context.Background(), ble.NewAddr(c.Args.Address))
	if err != nil {
		return fmt.Errorf("could not connect to '%s': %w", c.Args.Address, err)
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
	payload := make([]byte, 0, 10)
	payload = append(payload, arisble.RACP_opcode_report_records)
	if c.since != nil {
		if c.until != nil {
			payload = append(payload, arisble.RACP_operator_in_range)
			var err error
			payload, err = arisble.NewTimestamp(*c.since).AppendBinary(payload)
			if err != nil {
				return err
			}
			payload, err = arisble.NewTimestamp(*c.until).AppendBinary(payload)
			if err != nil {
				return err
			}
		} else {
			payload = append(payload, arisble.RACP_operator_ge)
			var err error
			payload, err = arisble.NewTimestamp(*c.since).AppendBinary(payload)
			if err != nil {
				return err
			}
		}
	} else if c.until != nil {
		var err error
		payload = append(payload, arisble.RACP_operator_le)
		payload, err = arisble.NewTimestamp(*c.until).AppendBinary(payload)
		if err != nil {
			return err
		}
	} else {
		payload = append(payload, arisble.RACP_operator_all)
	}

	err = conn.WriteCharacteristic(racpChar, []byte{arisble.RACP_opcode_report_records, arisble.RACP_operator_all}, false)
	if err != nil {
		return err
	}

	if c.Voltage == true {
		fmt.Printf("#timestamp,temperature(°C),humidity(%%),voltage_loaded(mV),voltage_open(mV),CO2(PPM)\n")
	} else {
		fmt.Printf("#timestamp,temperature(°C),humidity(%%),pressure(hPa),CO2(PPM)\n")
	}
	var wg sync.WaitGroup

	wg.Go(func() {
		for err := range *errors {
			slog.Error("got RACP error", slog.String("error", err.Error()))
		}
	})

	if c.Voltage == true {
		wg.Go(func() {
			for dp := range *points {
				loaded, open := dp.Pressure.ToVoltage()
				fmt.Printf("%s,%.2f,%.1f,%d,%d,%d\n",
					dp.Timestamp.ToTime().Format(time.RFC3339),
					dp.Temperature.Float64(),
					dp.Humidity.Float64(),
					loaded,
					open,
					dp.CO2)
			}
		})
	} else {
		wg.Go(func() {
			for dp := range *points {
				fmt.Printf("%s,%.2f,%.1f,%.3f,%d\n",
					dp.Timestamp.ToTime().Format(time.RFC3339),
					dp.Temperature.Float64(),
					dp.Humidity.Float64(),
					dp.Pressure.Float64(),
					dp.CO2)
			}
		})
	}

	wg.Wait()
	return nil
}

func init() {
	_, err := parser.AddCommand("download", "download backload data from a device", "download journal data from a device.", &DownloadCommand{})
	if err != nil {
		panic(err)
	}
}
