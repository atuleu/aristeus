package collector

import (
	"context"
	"encoding/binary"
	"fmt"
	"log/slog"
	"os"
	"path"
	"path/filepath"
	"sync"
	"time"

	"github.com/adrg/xdg"
	"github.com/atuleu/aristeus/go/pkg/arisble"
	"github.com/go-ble/ble"
)

type CollectorConfig struct {
	journal DataJournal // for dependency injection
	device  BLEDevice   // for dependency injection

	MinimumAssignementDuration time.Duration
	MaximalTimeOffset          time.Duration
	JanitorTime                HourOfDay
}

type Collector struct {
	mx           sync.RWMutex
	devices      map[string]*EnvironmentalDevice
	envPublisher Publisher[EnvironmentalDevice]

	hiveIDFilter map[uint8]bool

	logger *slog.Logger

	journal DataJournal
	scanner BLEScanner
}

func (c *Collector) bleAdvFilter() ble.AdvFilter {
	hiveIDFilter := make(map[uint8]bool)
	for ID, ok := range c.hiveIDFilter {
		hiveIDFilter[ID] = ok
	}
	return func(adv ble.Advertisement) bool {
		mdata := adv.ManufacturerData()
		if len(mdata) < 2 {
			return false
		}
		manufacturerID := binary.LittleEndian.Uint16(mdata[0:2])
		if len(mdata) == 20 && manufacturerID == 0xFFFF {
			if len(hiveIDFilter) > 0 {
				return hiveIDFilter[mdata[3]]
			}
			return true
		}
		return false
	}
}

func (c *Collector) onAdvertisment(ctx context.Context, adv EnvironmentalAdvertisment) {
	c.mx.Lock()
	defer c.mx.Unlock()

	d, ok := c.devices[adv.address.String()]
	if ok == false {
		c.onNewDevice(ctx, adv)
	} else {
		c.updateDevice(ctx, d, adv)
	}

}

const ASSIGNMENT_MINIMUM_PERIOD = 5 * time.Minute
const MAXIMAL_TIME_OFFSET = 3 * time.Minute

func (c *Collector) onNewDevice(ctx context.Context, adv EnvironmentalAdvertisment) {
	d, err := NewEnvironmentalDevice(adv)
	if err != nil {
		c.logger.Error("could not create new device", slog.String("error", err.Error()))
		return
	}

	ctx, cancel := context.WithTimeout(ctx, 5*time.Second)
	defer cancel()
	assignments, err := c.journal.GetLocationAssignements(ctx, d.Location.LocationID)
	if err != nil {
		c.logger.Error("could not retrieve assignments",
			slog.String("location_id", d.Location.LocationID),
			slog.String("error", err.Error()),
		)
		return
	}

	if len(assignments) > 0 {
		activeAssignments := assignments[len(assignments)-1]
		if activeAssignments.RemovedAt != nil &&
			activeAssignments.SensorID != adv.address.String() &&
			adv.data.CurrentPoint.Timestamp.ToTime().Sub(activeAssignments.InstalledAt) < ASSIGNMENT_MINIMUM_PERIOD {
			c.logger.Error("dropping new device discovery as location was assigned not too long ago",
				slog.String("address", adv.address.String()),
				slog.String("location_id", activeAssignments.LocationID),
				slog.String("current_device", activeAssignments.SensorID),
				slog.Time("since", activeAssignments.InstalledAt),
			)
			return
		}
	}

	c.logger.Info("new device found",
		slog.String("address", d.Address))

	c.devices[adv.address.String()] = d

	c.pushEnvironmentalUpdate(ctx, d, adv)
}

func (c *Collector) synchronizeDevice(targetAddress ble.Addr) BLETask {

	logger := c.logger.With(slog.String("target_address", targetAddress.String()))

	return func(ctx context.Context, dev BLEDevice) {
		ctxConn, cancel := context.WithTimeout(ctx, 60*time.Second)
		defer cancel()

		client, err := arisble.NewBLEDeviceConn(dev, ctxConn, targetAddress)
		if err != nil {
			logger.Error("could not connect to device for time synchronization",
				slog.String("error", err.Error()),
			)
			return
		}
		err = client.SynchronizeBLEDevice()
		if err != nil {
			logger.Error("could not synchronize device",
				slog.String("error", err.Error()),
			)
			return
		}
		logger.Info("synchronized device")
	}

}

func (c *Collector) updateDevice(ctx context.Context, dev *EnvironmentalDevice, adv EnvironmentalAdvertisment) {

	if adv.data.CurrentPoint.Timestamp.ToTime().After(dev.Current.Timestamp) == false {
		return
	}
	locationID := BuildLocationID(adv.data.Location)

	if locationID != dev.Location.LocationID &&
		adv.data.CurrentPoint.Timestamp.ToTime().Sub(dev.assigned_since) < ASSIGNMENT_MINIMUM_PERIOD {
		c.logger.Error("dropping update since device changed location too rapidly",
			slog.String("address", dev.Address),
			slog.String("current_location_id", dev.Location.LocationID),
			slog.String("new_location_id", locationID),
			slog.Time("assigned_since", dev.assigned_since),
		)
		return
	}

	if (dev.LastSeen != time.Time{}) {
		dev.AdvertismentPeriod = adv.receivedAt.Sub(dev.LastSeen).Round(time.Millisecond)
	}
	dev.LastSeen = adv.receivedAt

	if dev.Current.Timestamp == adv.data.CurrentPoint.Timestamp.ToTime() {
		return
	}

	dev.TimeOffset = adv.receivedAt.Sub(adv.data.CurrentPoint.Timestamp.ToTime())

	if dev.TimeOffset.Abs() > MAXIMAL_TIME_OFFSET {
		c.scanner.Schedule(c.synchronizeDevice(adv.address))
	}

	c.pushEnvironmentalUpdate(ctx, dev, adv)
}

func (c *Collector) pushEnvironmentalUpdate(ctx context.Context, dev *EnvironmentalDevice, adv EnvironmentalAdvertisment) {
	reading := dev.updateData(adv)

	c.envPublisher.Update(dev.clone())

	go func() {
		txContext, txCancel := context.WithTimeout(ctx, 5*time.Second)
		defer txCancel()
		err := c.journal.SaveEnvironmentalReadings(txContext, []EnvironmentalReading{reading})
		if err != nil {
			c.logger.Error("failed to save new reading",
				slog.String("address", adv.address.String()),
				slog.Time("timestamp", adv.data.CurrentPoint.Timestamp.ToTime()),
				slog.String("error", err.Error()),
			)
		}
	}()
}

func (c *Collector) Subscribe() (ch <-chan EnvironmentalDevice) {
	defer func() {
		c.logger.Info("subscribed", slog.Any("channel", ch))
	}()

	c.mx.RLock()
	devices := make([]EnvironmentalDevice, 0, len(c.devices))
	for _, d := range c.devices {
		devices = append(devices, d.clone())
	}
	c.mx.RUnlock()

	ch = c.envPublisher.Subscribe(devices)
	return
}

func (c *Collector) Unsubscribe(ch <-chan EnvironmentalDevice) error {
	defer c.logger.Info("unsubscribed", slog.Any("channel", ch))
	return c.envPublisher.Unsubscribe(ch)
}

// Returns the current list of EnvironmentalDevice currenctly beeing collected.
func (c *Collector) GetEnvironmentalDevices() []EnvironmentalDevice {
	c.mx.RLock()
	defer c.mx.RUnlock()
	res := make([]EnvironmentalDevice, 0, len(c.devices))
	for _, d := range c.devices {
		res = append(res, d.clone())
	}
	return res
}

// Connects to device to operate on it. Please not that timeout will be at most 5 minutes.
func (c *Collector) ConnectEnvironmentalDevice(addr ble.Addr, timeout time.Duration, fn func(client *arisble.BLEDeviceConn, err error)) error {
	c.mx.RLock()
	defer c.mx.RUnlock()

	_, ok := c.devices[addr.String()]
	if ok == false {
		return fmt.Errorf("device %s is not monitored", addr.String())
	}

	timeout = min(timeout, 5*time.Minute)

	c.scanner.Schedule(func(ctx context.Context, dev BLEDevice) {
		ctx, cancel := context.WithTimeout(ctx, timeout)
		defer cancel()
		conn, err := arisble.NewBLEDeviceConn(dev, ctx, addr)
		if err != nil {
			fn(nil, err)
		}
		defer conn.Close()
		fn(conn, nil)
	})

	return nil
}

// Runs the collection loops, i.e. gather BLE data, save it to journal, maintain
// a list of device we can control
func (c *Collector) Collect(ctx context.Context) error {

	var wg sync.WaitGroup
	defer wg.Wait()

	advs, errs, err := c.scanner.ScanLoop(ctx, c.bleAdvFilter())
	if err != nil {
		return err
	}

	wg.Go(func() {
		for adv := range advs {
			logger := c.logger.With(slog.String("address", adv.Adv.Addr().String()))
			eAdv := EnvironmentalAdvertisment{
				address:    adv.Adv.Addr(),
				receivedAt: adv.ReceivedAt,
			}

			if err := eAdv.data.UnmarshalBinary(adv.Adv.ManufacturerData()[2:]); err != nil {
				logger.Error("could not parse advertisement data",
					slog.String("error", err.Error()),
				)
			}

			wg.Go(func() { c.onAdvertisment(ctx, eAdv) })
		}
	})

	return <-errs
}

func (c *Collector) connectJournal() (DataJournal, error) {
	dbPath, err := xdg.DataFile(path.Join("io.github.atuleu.aristeus", "dababase"))
	if err != nil {
		return nil, fmt.Errorf("could not generate datapath: %w", err)
	}
	err = os.MkdirAll(filepath.Dir(dbPath), 0755)
	if err != nil {
		return nil, fmt.Errorf("could not create journal directories: %w", err)
	}

	ctx, cancel := context.WithTimeout(context.Background(), 10*time.Second)
	defer cancel()
	journal, err := NewSQLiteStore(ctx, dbPath)
	if err != nil {
		return nil, fmt.Errorf("could not open journal `%s`: %w", dbPath, err)
	}
	return journal, nil
}

// Creates a new collector.
func NewCollector(hiveIDs []uint8, journal DataJournal, dev BLEDevice) (*Collector, error) {
	res := &Collector{
		devices:      make(map[string]*EnvironmentalDevice),
		hiveIDFilter: make(map[uint8]bool),
		logger:       slog.With(slog.String("module", "collector")),
	}

	for _, hiveID := range hiveIDs {
		res.hiveIDFilter[hiveID] = true
	}

	var err error
	if journal == nil {
		journal, err = res.connectJournal()
		if err != nil {
			return nil, err
		}
	}

	if dev == nil {
		dev, err = arisble.NewBLEDevice()
		if err != nil {
			return nil, fmt.Errorf("could not open BLE device: %w", err)
		}
	}

	res.journal = journal
	res.scanner = NewScanner(dev)

	ctx, cancel := context.WithTimeout(context.Background(), 10*time.Second)
	defer cancel()

	assignments, err := res.journal.GetActiveAssignments(ctx)

	if err != nil {
		return nil, fmt.Errorf("could not retrieve active assignment: %w", err)
	}

	for _, a := range assignments {
		d, err := NewAssignedEnvironmentalDevice(a)
		if err != nil {
			slog.Error("could not retrieve stored assignments",
				slog.String("error", err.Error()),
				slog.String("location_id", a.LocationID),
				slog.String("sensor_id", a.SensorID),
				slog.Time("installed_at", a.InstalledAt),
			)
			continue
		}
		res.devices[d.Address] = d
	}

	return res, nil

}
