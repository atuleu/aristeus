package emc2101

import (
	"testing"

	"github.com/stretchr/testify/assert"
)

func TestRegisterAddressFormatting(t *testing.T) {

	testdata := []struct {
		expected string
		value    RegisterAddress
	}{
		{"ConfigurationRegister", ConfigurationRegister},
		{"LUTSetTemperature1Register", LUTSetTemperature1Register},
		{"LUTSetPWM1Register", LUTSetPWM1Register},
		{"LUTSetTemperature2Register", LUTSetTemperature1Register + RegisterAddress(2)},
		{"LUTSetPWM8Register", LUTSetPWM1Register + RegisterAddress(14)},
	}

	for _, d := range testdata {
		t.Run(d.expected, func(t *testing.T) {
			assert := assert.New(t)
			assert.Equal(d.expected, d.value.String())
		})
	}
}

func TestConfigurationRegisterValue(t *testing.T) {
	testdata := []struct {
		name     string
		config   DeviceConfiguration
		expected byte
	}{
		{name: "default", config: DeviceConfiguration{}, expected: 0x04},
		{name: "device-POR", config: DeviceConfiguration{UseInterruptPin: true}, expected: 0x00},
	}

	for _, d := range testdata {
		t.Run(d.name, func(t *testing.T) {
			assert := assert.New(t)
			assert.Equal(d.expected, d.config.RegisterValue(false))
		})
	}
}
