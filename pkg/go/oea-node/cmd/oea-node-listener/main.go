package main

import (
	"context"
	"encoding/binary"
	"fmt"
	"log/slog"
	"os"
	"os/signal"
	"strconv"
	"time"

	"github.com/go-ble/ble"
	ble_linux "github.com/go-ble/ble/linux"
)

func ErrAttr(err error) slog.Attr {
	return slog.Any("error", err)
}

func main() {
	if err := execute(); err != nil {
		slog.Error("unhandled error", ErrAttr(err))
		os.Exit(1)
	}
}

type BatteryLevel uint8

func (b BatteryLevel) String() string {
	if b == 0xff {
		return "NaN"
	}
	return strconv.FormatInt(int64(b), 10) + "%"
}

type Location struct {
	HiveID   uint8
	Location uint8
}

func (l Location) String() string {
	return fmt.Sprintf("location{hive:%d location:%d}", l.HiveID, l.Location)
}

type FlashLevel uint8

func (l FlashLevel) String() string {
	return strconv.FormatFloat(100.0*float64(l)/255.0, 'f', 1, 64) + "%"
}

type Temperature int16

func (t Temperature) String() string {
	return strconv.FormatFloat(float64(t)/100.0, 'f', 2, 64) + "°C"
}

type Humidity uint16

func (h Humidity) String() string {
	return strconv.FormatFloat(float64(h)/10.0, 'f', 1, 64) + "%"
}

type Pressure uint32

func (p Pressure) String() string {
	return strconv.FormatFloat(float64(p)/1000.0, 'f', 3, 64) + "hPa"
}

type CO2Concentration uint16

func (c CO2Concentration) String() string {
	return strconv.FormatInt(int64(c), 10) + "PPM"
}

type DataPoint struct {
	Timestamp   time.Time
	Temperature Temperature
	Humidity    Humidity
	Pressure    Pressure
	CO2         CO2Concentration
}

func ParseDatapoint(b []byte) (DataPoint, error) {
	res := DataPoint{
		Timestamp:   0xffffffff,
		Temperature: -0x8000,
		Humidity:    0xffff,
		Pressure:    0xFFFFFFFF,
		CO2:         0xffff,
	}
	if len(b) < 14 {
		return res, fmt.Errorf("unsufficient size")
	}
	res.Timestamp = time.Unix(int64(binary.LittleEndian.Uint32(b[0:4])), 0)
	res.Temperature = Temperature(binary.LittleEndian.Uint16(b[4:6]))
	res.Humidity = Humidity(binary.LittleEndian.Uint16(b[6:8]))
	res.Pressure = Pressure(binary.LittleEndian.Uint32(b[8:12]))
	res.CO2 = CO2Concentration(binary.LittleEndian.Uint16(b[12:14]))
	return res, nil
}

type AdvertisementData struct {
	Location Location
	Battery  BatteryLevel
	Memory   FlashLevel
	Data     DataPoint
}

func ParseAdvertisement(b []byte) (AdvertisementData, error) {
	res := AdvertisementData{
		Location: Location{0, 0},
		Battery:  0xff,
		Memory:   0x00,
	}
	if len(b) < 18 {
		return res, fmt.Errorf("insufficient size")
	}

	res.Location.HiveID = b[0]
	res.Location.Location = b[1]
	res.Battery = BatteryLevel(b[2])
	res.Memory = FlashLevel(b[3])
	var err error
	res.Data, err = ParseDatapoint(b[4:])
	return res, err
}

func onAdvertisement(adv ble.Advertisement) {
	d, err := ParseAdvertisement(adv.ManufacturerData()[2:])
	if err != nil {
		slog.Warn("ill-formed adv", ErrAttr(err))
		return
	}

	slog.Info("received advertisement",
		slog.Time("time", d.Data.Timestamp),
		slog.String("location", d.Location.String()),
		slog.String("battery", d.Battery.String()),
		slog.String("memory", d.Memory.String()),
		slog.String("temperature", d.Data.Temperature.String()),
		slog.String("humidity", d.Data.Humidity.String()),
		slog.String("pressure", d.Data.Pressure.String()),
		slog.String("co2", d.Data.CO2.String()),
	)

	if time.Now().Sub(d.Data.Timestamp).Abs() > 2*time.Minute {
		slog.Warn("device out of sync")

	}

}

func advertisementFilter(adv ble.Advertisement) bool {
	mdata := adv.ManufacturerData()
	if len(mdata) < 2 {
		return false
	}
	return mdata[0] == 0xff && mdata[1] == 0xff
}

func execute() error {

	dev, err := ble_linux.NewDevice()
	if err != nil {
		return fmt.Errorf("could not access BLE device: %w", err)
	}
	ble.SetDefaultDevice(dev)

	ctx, stop := signal.NotifyContext(context.Background(), os.Interrupt)
	defer stop()

	err = ble.Scan(ctx, false, onAdvertisement, advertisementFilter)

	if err != nil && err != context.Canceled {
		return fmt.Errorf("scan failure: %w", err)
	}

	return nil
}
