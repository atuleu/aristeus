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
