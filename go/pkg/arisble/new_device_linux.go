package arisble

import (
	"github.com/go-ble/ble"
	ble_linux "github.com/go-ble/ble/linux"
)

func NewBLEDevice() (ble.Device, error) {
	return ble_linux.NewDevice()
}
