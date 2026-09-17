package emc2101

import (
	"cmp"
	"errors"
	"fmt"
	"math"
	"slices"

	"periph.io/x/conn/v3/i2c"
)

//go:generate stringer -type=RegisterAddress

type RegisterAddress byte

const (
	InternalTemperatureRegister           RegisterAddress = 0x00
	ExternalTemperatureHighBRegister      RegisterAddress = 0x01
	StatusRegister                        RegisterAddress = 0x02
	ConfigurationRegister                 RegisterAddress = 0x03
	ConversionRateRegister                RegisterAddress = 0x04
	InternalTemperatureLimitRegister      RegisterAddress = 0x05
	ExternalTemperatureLimitHighBRegister RegisterAddress = 0x06
	ExternalTemperatureLimitLowBRegister  RegisterAddress = 0x07
	ExternalTemperatureForceRegister      RegisterAddress = 0x08
	OneShotRegister                       RegisterAddress = 0x09
	ExternalTemperatureLowBRegister       RegisterAddress = 0x10
	ExternalDiodeLimitHighBRegister       RegisterAddress = 0x13
	ExternalDiodeLimitLowBRegister        RegisterAddress = 0x14
	AlertMaskRegister                     RegisterAddress = 0x16
	ExternalDiodeIdealityFactorRegister   RegisterAddress = 0x17
	BetaCompensationFactorRegister        RegisterAddress = 0x18
	TCritTempLimitRegister                RegisterAddress = 0x19
	TCritHisteresysRegister               RegisterAddress = 0x21
	TachReadingLowBRegister               RegisterAddress = 0x46
	TachReadingHighBRegister              RegisterAddress = 0x47
	TachLimitLowBRegister                 RegisterAddress = 0x48
	TachLimitHighBRegister                RegisterAddress = 0x49
	FanConfigurationRegister              RegisterAddress = 0x4a
	FanSpinUpRegister                     RegisterAddress = 0x4b
	FanSettingRegister                    RegisterAddress = 0x4c
	PWMFrequencyRegister                  RegisterAddress = 0x4d
	PWMFrequencyDivideRegister            RegisterAddress = 0x4e
	LUTHysteresisRegister                 RegisterAddress = 0x4f
	LUTSetTemperature1Register            RegisterAddress = 0x50
	LUTSetPWM1Register                    RegisterAddress = 0x51
	LUTSetTemperature2Register            RegisterAddress = 0x52
	LUTSetPWM2Register                    RegisterAddress = 0x53
	LUTSetTemperature3Register            RegisterAddress = 0x54
	LUTSetPWM3Register                    RegisterAddress = 0x55
	LUTSetTemperature4Register            RegisterAddress = 0x56
	LUTSetPWM4Register                    RegisterAddress = 0x57
	LUTSetTemperature5Register            RegisterAddress = 0x58
	LUTSetPWM5Register                    RegisterAddress = 0x59
	LUTSetTemperature6Register            RegisterAddress = 0x5a
	LUTSetPWM6Register                    RegisterAddress = 0x5b
	LUTSetTemperature7Register            RegisterAddress = 0x5c
	LUTSetPWM7Register                    RegisterAddress = 0x5d
	LUTSetTemperature8Register            RegisterAddress = 0x5e
	LUTSetPWM8Register                    RegisterAddress = 0x5f
	AveragingFilterRegister               RegisterAddress = 0xbf
	ProductIDRegister                     RegisterAddress = 0xfd
	ManufacturerIDRegister                RegisterAddress = 0xfe
	RevisionRegister                      RegisterAddress = 0xff
)

const (
	StatusTachBM byte = 1 << 0
	StatusTCritBM
	StatusFaultBM
	StatusExtLowBM
	StatusExtHighBM
	StatusEEPROMBM
	StatusIntHighBM
	StatusBusyBM
)

const (
	ConfigurationQueueBM byte = 1 << 0
	ConfigurationTCritOVRDBM
	ConfigurationAltTachBM
	ConfigurationDisTOBM
	ConfigurationDACBM
	ConfigurationFanStandbyBM
	ConfigurationStandby
	ConfigurationMask
)

const (
	ConversionRateBM byte = 0x0f
)

const ExternalIdealityFactorBM byte = 0x3f

const (
	BetaCompensationBM      byte = 0x07
	BetCompensationEnableBM byte = 1 << 3
)

const (
	FanConfigurationTachMBM    byte = 0x03
	FanConfigurationClckOvrBM  byte = 1 << 2
	FanConfigurationClckSelBM  byte = 1 << 3
	FanConfigurationPolarityBM byte = 1 << 4
	FanConfigurationProgBM     byte = 1 << 5
	FanConfigurationForceBM    byte = 1 << 6
)

const (
	FanSpinUpTimeBM      byte = 0x07
	FanSpinUpSpinDriveBM byte = 0x03 << 3
	FanSpinUpFastTachBM  byte = 1 << 5
)

var errNYI = errors.New("not implemented error")

type FanConfiguration struct {
	FanPolarityInverted bool
	FanSlowClock        bool
	FanClockOverride    bool
}

func (c FanConfiguration) RegisterValue(prog bool) byte {
	res := byte(0x00)
	if prog {
		res |= FanConfigurationProgBM
	}
	if c.FanPolarityInverted {
		res |= FanConfigurationPolarityBM
	}
	if c.FanSlowClock {
		res |= FanConfigurationClckSelBM
	}
	if c.FanClockOverride {
		res |= FanConfigurationClckOvrBM
	}
	return res
}

type DeviceConfiguration struct {
	MaskAlert               bool
	FanStandbyDisableOutput bool
	FanOutputDAC            bool
	DisableSMBusTimeout     bool
	UseInterruptPin         bool
	EnableTCritLimit        bool
	QueueAlert              bool
}

func (c DeviceConfiguration) RegisterValue(standby bool) byte {
	res := byte(0)
	if c.QueueAlert {
		res |= ConfigurationQueueBM
	}
	if c.EnableTCritLimit {
		res |= ConfigurationTCritOVRDBM
	}
	if !c.UseInterruptPin {
		res |= ConfigurationAltTachBM
	}
	if c.DisableSMBusTimeout {
		res |= ConfigurationDisTOBM
	}
	if c.FanOutputDAC {
		res |= ConfigurationDACBM
	}
	if c.FanStandbyDisableOutput {
		res |= ConfigurationFanStandbyBM
	}
	if standby {
		res |= ConfigurationStandby
	}
	if c.MaskAlert {
		res |= ConfigurationMask
	}
	return res

}

type Config struct {
	FanConfiguration
	DeviceConfiguration
}

type Device struct {
	dev    i2c.Dev
	config Config
}

func NewDevice(bus i2c.Bus, config Config) (*Device, error) {
	res := &Device{dev: i2c.Dev{Bus: bus, Addr: 0x4c}, config: config}

	productID, err := res.ReadRegister(ProductIDRegister)
	if err != nil {
		return nil, fmt.Errorf("could not read product ID: %w", err)
	}
	if productID != 0x16 && productID != 0x28 {
		return nil, fmt.Errorf("invalid product id 0x%02X, expected 0x16 or 0x28", productID)
	}
	manufacturerID, err := res.ReadRegister(ManufacturerIDRegister)
	if err != nil {
		return nil, fmt.Errorf("could not read manufacturer ID: %w", err)
	}
	if manufacturerID != 0x5D {
		return nil, fmt.Errorf("invalid manufacturer ID 0x%02X, expected: 0x5D", manufacturerID)
	}

	err = res.WriteRegister(ConfigurationRegister, res.config.DeviceConfiguration.RegisterValue(false))
	if err != nil {
		return nil, fmt.Errorf("could not set device configuration: %w", err)
	}
	return res, nil
}

func (d *Device) ReadRegister(address RegisterAddress) (byte, error) {
	write := []byte{byte(address)}
	read := []byte{0xff}
	err := d.dev.Tx(write, read)
	if err != nil {
		return 0xff, fmt.Errorf("could not read emc2101.%s: %w", address, err)
	}
	return read[0], nil
}

func (d *Device) WriteRegister(address RegisterAddress, value byte) error {
	err := d.dev.Tx([]byte{byte(address), value}, nil)
	if err != nil {
		return fmt.Errorf("could not write emc2101.%s: %w", address, err)
	}
	return nil
}

type LUTPoint struct {
	Temperature int8
	PWM         float64
}

func (d *Device) SetLUTTable(table []LUTPoint) error {
	if len(table) == 0 {
		return errors.New("empty table")
	}
	if len(table) > 8 {
		return fmt.Errorf("maximal table size is 8, got: %d", len(table))
	}

	for i, pt := range table {
		if pt.PWM < 0.0 || pt.PWM > 100.0 {
			return fmt.Errorf("invalid point %d: %v: PWM must be in [0,100.0]", i, pt)
		}
	}

	for len(table) < 8 {
		table = append(table, LUTPoint{Temperature: 127, PWM: 100.0})
	}

	slices.SortStableFunc(table, func(a, b LUTPoint) int {
		return cmp.Compare(a.Temperature, b.Temperature)
	})

	err := d.WriteRegister(FanConfigurationRegister, d.config.FanConfiguration.RegisterValue(true))
	if err != nil {
		return fmt.Errorf("could not switch to program mode: %w", err)
	}

	for i, pt := range table {
		err := d.WriteRegister(LUTSetTemperature1Register+RegisterAddress(2*i), byte(pt.Temperature))
		if err != nil {
			return fmt.Errorf("could not set point %d temperature: %w", i+1, err)
		}
		err = d.WriteRegister(LUTSetPWM1Register+RegisterAddress(2*i), byte(pt.PWM/100.0*63.0))
		if err != nil {
			return fmt.Errorf("could not set point %d PWM: %w", i+1, err)
		}
	}

	err = d.WriteRegister(FanConfigurationRegister, d.config.FanConfiguration.RegisterValue(false))
	if err != nil {
		return fmt.Errorf("could not set LUT based control mode: %w", err)
	}

	return nil
}

func (d *Device) ReadTemperature() (float64, error) {
	high, err := d.ReadRegister(ExternalTemperatureHighBRegister)
	if err != nil {
		return math.NaN(), err
	}
	low, err := d.ReadRegister(ExternalTemperatureLowBRegister)
	if err != nil {
		return math.NaN(), err
	}
	var res float64 = float64(int8(high)) + float64(low>>5)*0.125
	return res, nil
}

func (d *Device) ReadRPM() (uint, error) {
	high, err := d.ReadRegister(TachReadingHighBRegister)
	if err != nil {
		return 0, err
	}
	low, err := d.ReadRegister(TachReadingLowBRegister)
	if err != nil {
		return 0, err
	}

	value := (uint(high) << 8) | (uint(low))
	return 5400000 / value, nil
}
