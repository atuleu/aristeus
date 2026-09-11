package collector

import (
	"context"
	"encoding/binary"
	"errors"
	"fmt"
	"log/slog"
	"math"
	"math/rand"
	"sync"
	"time"

	"github.com/atuleu/aristeus/go/pkg/arisble"
	"github.com/go-ble/ble"
)

type environmentalOperator interface {
	SynchronizeDevice(ctx context.Context, dev BLEDevice, address ble.Addr) error
	ReadJournalAndEraseDevice(ctx context.Context, dev BLEDevice, address ble.Addr, locationID string, journal DataJournal) error
}

type Collector struct {
	config       CollectorConfig
	wg           sync.WaitGroup
	mx           sync.RWMutex
	devices      map[string]*EnvironmentalDevice
	envPublisher Publisher[EnvironmentalDevice]

	hiveIDFilter map[uint8]bool

	logger *slog.Logger

	journal               DataJournal
	scanner               BLEScanner
	cron                  CronScheduler
	environmentalOperator environmentalOperator
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

func (c *Collector) onNewDevice(ctx context.Context, adv EnvironmentalAdvertisment) {
	d, err := NewEnvironmentalDevice(adv)
	if err != nil {
		c.logger.Error("could not create new device", slog.String("error", err.Error()))
		return
	}

	ctxCheck, cancel := context.WithTimeout(ctx, 5*time.Second)
	defer cancel()
	assignments, err := c.journal.GetLocationAssignements(ctxCheck, d.Location.LocationID)
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
			adv.data.CurrentPoint.Timestamp.ToTime().Sub(activeAssignments.InstalledAt) < c.config.MinimumAssignementDuration {
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

func (c *Collector) updateDevice(ctx context.Context, dev *EnvironmentalDevice, adv EnvironmentalAdvertisment) {
	locationID := BuildLocationID(adv.data.Location)

	if locationID != dev.Location.LocationID &&
		adv.data.CurrentPoint.Timestamp.ToTime().Sub(dev.assignedSince) < c.config.MinimumAssignementDuration {
		c.logger.Error("dropping update since device changed location too rapidly",
			slog.String("address", dev.Address),
			slog.String("current_location_id", dev.Location.LocationID),
			slog.String("new_location_id", locationID),
			slog.Time("assigned_since", dev.assignedSince),
		)
		return
	}

	if (dev.LastSeen != time.Time{}) {
		dev.AdvertisementPeriod = adv.receivedAt.Sub(dev.LastSeen).Round(time.Millisecond)
	}

	dev.LastSeen = adv.receivedAt

	if adv.data.CurrentPoint.Timestamp.ToTime().After(dev.Current.Timestamp) == false {
		return
	}

	c.logger.Debug("new update",
		slog.String("address", dev.Address),
		slog.Time("timestamp", adv.data.CurrentPoint.Timestamp.ToTime()))

	c.pushEnvironmentalUpdate(ctx, dev, adv)
}

func (c *Collector) pushEnvironmentalUpdate(ctx context.Context, dev *EnvironmentalDevice, adv EnvironmentalAdvertisment) {
	reading := dev.updateData(adv)

	logger := c.logger.With(
		slog.String("address", adv.address.String()),
		slog.String("location_id", BuildLocationID(adv.data.Location)),
		slog.Time("timestamp", adv.data.CurrentPoint.Timestamp.ToTime()),
	)

	if c.config.SynchronizeDevices == true && dev.TimeOffset.Abs() > c.config.MaximalTimeOffset {
		logger.Warn("device out of sync",
			slog.Duration("time_offset", dev.TimeOffset),
		)
		if err := c.scanner.Schedule(c.synchronizeEnvironmentalDeviceTask(adv.address)); err != nil {
			logger.Error("could not schedule synchronization",
				slog.String("error", err.Error()),
			)
		}
	}

	if err := c.envPublisher.Update(dev.clone()); err != nil {
		c.logger.Warn("could not push update",
			slog.String("error", err.Error()))
	} else {
		c.logger.Debug("pushing update",
			slog.String("address", adv.address.String()))
	}

	c.wg.Go(func() {
		logger.Debug("writing to journal")
		txContext, txCancel := context.WithTimeout(ctx, 5*time.Second)
		defer txCancel()

		err := c.journal.SaveEnvironmentalReadings(txContext, []EnvironmentalReading{reading})
		if err != nil {
			logger.Error("failed to save new reading",
				slog.String("error", err.Error()),
			)
		} else {
			logger.Debug("saved")
		}
	})
}

func (c *Collector) Subscribe(capacity int) (ch <-chan EnvironmentalDevice) {
	defer func() {
		c.logger.Info("subscribed", slog.Any("channel", ch))
	}()

	c.mx.RLock()
	defer c.mx.RUnlock()

	devices := make([]EnvironmentalDevice, 0, len(c.devices))
	for _, d := range c.devices {
		devices = append(devices, d.clone())
	}

	return c.envPublisher.Subscribe(devices, capacity)
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

	return c.scanner.Schedule(func(ctx context.Context, dev BLEDevice) {
		ctx, cancel := context.WithTimeout(ctx, timeout)
		defer cancel()
		conn, err := arisble.NewBLEDeviceConn(dev, ctx, addr)
		if err != nil {
			fn(nil, err)
			return
		}
		defer conn.Close()
		fn(conn, nil)
	})
}

// Runs the collection loops, i.e. gather BLE data, save it to journal, maintain
// a list of device we can control
func (c *Collector) Collect(ctx context.Context) error {

	defer c.wg.Wait()

	advs, errs, err := c.scanner.ScanLoop(ctx, c.bleAdvFilter())
	if err != nil {
		return err
	}

	c.wg.Go(func() {
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

			c.onAdvertisment(ctx, eAdv)
		}
	})

	c.wg.Go(func() {
		serveCollector(ctx, c)
	})

	c.wg.Go(func() {
		c.logger.Info("starting CRON Job",
			slog.Int("hour", c.config.JanitorTime.Hour),
			slog.Int("minute", c.config.JanitorTime.Minute),
		)
		c.cron.ScheduleLoop(ctx, c.config.JanitorTime, c.janitorTasks)
	})

	return <-errs
}

func (c *Collector) janitorTasks(ctx context.Context, now time.Time) {
	c.mx.Lock()
	defer c.mx.Unlock()

	activeThreshold := now.Add(-c.config.ActiveThresholdDuration)
	for addr, envDev := range c.devices {
		if envDev.LastSeen.Before(activeThreshold) {
			c.logger.Info("not performing janitor task, as device seems inactive",
				slog.String("address", envDev.Address),
				slog.Time("last_seen", envDev.LastSeen),
			)
			continue
		}

		memoryUsage := math.NaN()
		if envDev.MemoryUsage != nil {
			memoryUsage = *envDev.MemoryUsage
		}
		locationID := envDev.Location.LocationID
		targetAddress := ble.NewAddr(addr)
		logger := c.logger.With(slog.String("address", targetAddress.String()))
		c.wg.Go(func() {
			delay := time.Duration(rand.Int63n(c.config.ConnectionJitter.Nanoseconds()))
			timer := time.NewTimer(delay)
			defer timer.Stop()
			select {
			case <-timer.C:
			case <-ctx.Done():
				logger.Error("could not perform janitor task for device",
					slog.String("address", targetAddress.String()),
					slog.String("error", ctx.Err().Error()),
				)
				return
			}

			if memoryUsage >= 95 {
				if err := c.scanner.Schedule(c.readAndEraseEnvironmentalDeviceMemoryTask(targetAddress, locationID)); err != nil {
					logger.Error("could not schedule read and erase for device",
						slog.String("error", err.Error()),
					)
				}
			}
			if err := c.scanner.Schedule(c.synchronizeEnvironmentalDeviceTask(targetAddress)); err != nil {
				logger.Error("could not schedule device synchronization",
					slog.String("error", err.Error()),
				)
			}
		})
	}
}

func (c *Collector) readAndEraseEnvironmentalDeviceMemoryTask(addr ble.Addr, locationID string) BLETask {
	logger := c.logger.With(slog.String("address", addr.String()))
	return func(ctx context.Context, dev BLEDevice) {
		err := c.environmentalOperator.ReadJournalAndEraseDevice(ctx, dev, addr, locationID, c.journal)
		if err != nil {
			logger.Error("could not purge device data",
				slog.String("error", err.Error()),
			)
		} else {
			c.logger.Info("purged device memory")
		}
	}
}

func (c *Collector) synchronizeEnvironmentalDeviceTask(addr ble.Addr) BLETask {
	logger := c.logger.With(slog.String("address", addr.String()))
	return func(ctx context.Context, dev BLEDevice) {
		err := c.environmentalOperator.SynchronizeDevice(ctx, dev, addr)
		if err != nil {
			logger.Error("could not synchronize device",
				slog.String("error", err.Error()),
			)
		} else {
			c.logger.Info("synchronized device")
		}
	}
}

type environmentalOperatorImpl struct {
}

func (eoi environmentalOperatorImpl) ReadJournalAndEraseDevice(ctx context.Context, dev BLEDevice, addr ble.Addr, locationID string, journal DataJournal) error {
	ctxConn, cancel := context.WithTimeout(ctx, 5*time.Minute)
	defer cancel()
	client, err := arisble.NewBLEDeviceConn(dev, ctxConn, addr)
	if err != nil {
		return fmt.Errorf("could not connect to device: %w", err)
	}
	defer client.Close()

	reports, err := client.ReportRecords(arisble.TimestampNaN, arisble.TimestampNaN)
	var readings []EnvironmentalReading
	reading := EnvironmentalReading{
		LocationID: locationID,
		SensorID:   addr.String(),
	}
	notDone := true
	for notDone {
		var result arisble.RACPResult[arisble.DataPoint]
		select {
		case <-ctxConn.Done():
			return fmt.Errorf("could not retrieve device data: %w", err)
		case result, notDone = <-reports:
			if result.Error != nil {
				return fmt.Errorf("error while retrieving device data: %w", err)
			}
			reading.ReceivedAt = time.Now()
			reading.setData(result.Value)
			readings = append(readings, reading)
		}
	}
	err = journal.SaveEnvironmentalReadings(ctx, readings)
	if err != nil {
		return fmt.Errorf("could not save device data to journal: %w", err)
	}

	errs, err := client.DeleteRecords(arisble.TimestampNaN, arisble.TimestampNaN)
	if err != nil {
		return fmt.Errorf("could not send delete command to device: %w", err)
	}
	select {
	case <-ctxConn.Done():
		err = ctx.Err()
	case err, notDone = <-errs:
		if notDone == false {
			err = errors.New("early delete termination")
		}
	}
	if err != nil {
		return fmt.Errorf("could not delete device's data: %w", err)
	}
	return nil
}

func (eoi environmentalOperatorImpl) SynchronizeDevice(ctx context.Context, dev BLEDevice, targetAddress ble.Addr) error {
	ctxConn, cancel := context.WithTimeout(ctx, 2*time.Minute)
	defer cancel()

	client, err := arisble.NewBLEDeviceConn(dev, ctxConn, targetAddress)
	if err != nil {
		return fmt.Errorf("could not connect to device: %w", err)
	}
	defer client.Close()

	err = client.SynchronizeBLEDevice()
	if err != nil {
		return fmt.Errorf("could not synchronize device: %w", err)
	}
	return nil
}

// Creates a new collector.
func NewCollector(config CollectorConfig) (*Collector, error) {

	res := &Collector{
		config:       config,
		devices:      make(map[string]*EnvironmentalDevice),
		hiveIDFilter: make(map[uint8]bool),
		logger:       slog.With(slog.String("module", "collector")),
	}

	err := res.config.deps.doMissingInjection()
	if err != nil {
		return nil, err
	}

	for hiveID, collect := range config.HiveIDFilter {
		res.hiveIDFilter[hiveID] = collect
	}

	res.journal = res.config.deps.journal
	res.scanner = res.config.deps.scanner
	res.cron = res.config.deps.cron
	res.environmentalOperator = res.config.deps.environmentalOperator
	res.config.deps = nil

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
