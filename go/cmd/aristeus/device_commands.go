package main

import (
	"fmt"
	"log/slog"
	"strings"

	"github.com/atuleu/aristeus/go/pkg/arisble"
	"github.com/go-ble/ble"
)

type DeviceOptions struct {
	Args struct {
		Address string `required:"yes" description:"BLE address of the device to address"`
	} `positional-args:"yes"`
}

var deviceOptions = &DeviceOptions{}

type SetEPOCHCommand struct {
}

func (c *SetEPOCHCommand) Execute(args []string) error {
	conn, logger, cancel, err := dialDevice(ble.NewAddr(deviceOptions.Args.Address))
	defer cancel()
	if err != nil {
		return fmt.Errorf("could not connect to `%s`: %w", deviceOptions.Args.Address, err)
	}
	defer conn.Close()
	logger.Info("synchronizing")
	return conn.SynchronizeBLEDevice()
}

type GetLocationCommand struct {
}

func (c *GetLocationCommand) Execute(args []string) error {
	conn, logger, cancel, err := dialDevice(ble.NewAddr(deviceOptions.Args.Address))
	defer cancel()
	if err != nil {
		return fmt.Errorf("could not connect to `%s`: %w", deviceOptions.Args.Address, err)
	}
	defer conn.Close()
	logger.Info("getting location")
	location, err := conn.GetLocation()
	if err != nil {
		return err
	}
	fmt.Printf("Location: %s\n", location)
	return nil
}

type SetLocationCommand struct {
	Args struct {
		HiveID    uint8    `required:"yes"`
		Placement []string `required:"yes"`
	} `positional-args:"yes"`
}

func (c *SetLocationCommand) Execute(args []string) error {

	placement, err := arisble.ParsePlacement(strings.Join(c.Args.Placement, " "))
	if err != nil {
		return err
	}

	conn, logger, cancel, err := dialDevice(ble.NewAddr(deviceOptions.Args.Address))
	defer cancel()
	if err != nil {
		return fmt.Errorf("could not connect to `%s`: %w", deviceOptions.Args.Address, err)
	}
	defer conn.Close()
	logger.Info("setting location")
	return conn.SetLocation(arisble.Location{
		HiveID:    c.Args.HiveID,
		Placement: placement,
	})
}

type SetPressureCommand struct {
	CO2Recalibrate uint `long:"calibrate" description:"recalibrate STCC4 after update using this PPM value"`
	Args           struct {
		Pressure float64 `required:"yes"`
	} `positional-args:"yes"`
}

func (c *SetPressureCommand) Execute(args []string) error {
	if c.Args.Pressure < 800.0 || c.Args.Pressure > 1100.0 {
		return fmt.Errorf("desired pressure (%.3fhPa) seems out of range for barometric pressure", c.Args.Pressure)
	}
	if c.CO2Recalibrate != 0 && (c.CO2Recalibrate < 300 || c.CO2Recalibrate > 32000) {
		return fmt.Errorf("invalid PPM value (%d) for CO2 calibration must be in [300,32000]", c.CO2Recalibrate)
	}
	conn, logger, cancel, err := dialDevice(ble.NewAddr(deviceOptions.Args.Address))
	defer cancel()
	if err != nil {
		return fmt.Errorf("could not connect to `%s`: %w", deviceOptions.Args.Address, err)
	}
	defer conn.Close()
	CO2Target := arisble.CO2ConcentrationNaN
	if c.CO2Recalibrate != 0 {
		CO2Target = arisble.CO2Concentration(c.CO2Recalibrate)
	}
	logger.Info("setting pressure", slog.Int("CO2_recalibration", int(c.CO2Recalibrate)), slog.Float64("pressure", c.Args.Pressure))
	return conn.SetPressure(arisble.NewPressure(c.Args.Pressure), CO2Target)
}

func init() {
	cmd, err := parser.AddCommand("device",
		"device management commands",
		"commands to manage devices individually",
		deviceOptions)
	if err != nil {
		panic(err.Error())
	}
	_, err = cmd.AddCommand("set_epoch",
		"set EPOCH of target device",
		"set EPOCH of the device to current time",
		&SetEPOCHCommand{})
	if err != nil {
		panic(err.Error())
	}

	_, err = cmd.AddCommand("set_location",
		"set Location of the device",
		"set Location field on the device",
		&SetLocationCommand{})
	if err != nil {
		panic(err.Error())
	}

	_, err = cmd.AddCommand("get_location",
		"get location of the device",
		"get location field on the device",
		&SetLocationCommand{})
	if err != nil {
		panic(err.Error())
	}

	_, err = cmd.AddCommand("set_pressure",
		"tare the current barometric pressure",
		"set the current measured barometric pressure",
		&SetPressureCommand{})
	if err != nil {
		panic(err.Error())
	}

}
