package ble_sensor

type Location struct {
	HiveID   uint8
	Location uint8
}

type AdvertisementData struct {
	ADType        uint8
	ManufactureID uint8
	Location      Location
}
