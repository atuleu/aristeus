package collector

import (
	"context"
	"log/slog"
	"sync"
	"testing"
	"time"

	"github.com/atuleu/aristeus/go/pkg/arisble"
	"github.com/go-ble/ble"
	"github.com/stretchr/testify/mock"
	"github.com/stretchr/testify/suite"
)

type CollectorSuite struct {
	suite.Suite
	journal               *MockDataJournal
	scanner               *MockBLEScanner
	cron                  *MockCronScheduler
	environmentalOperator *mockenvironmentalOperator
	cronTick              chan time.Time
	collector             *Collector
	bleAdvertisment       chan TimedAdvertisement
	collectError          chan error
	ctx                   context.Context
	cancel                context.CancelFunc
}

type stubBLEAdvertisment struct {
	address          ble.Addr
	manufacturerData []byte
}

func (adv stubBLEAdvertisment) LocalName() string {
	return "device " + adv.address.String()
}
func (adv stubBLEAdvertisment) Addr() ble.Addr {
	return adv.address
}
func (adv stubBLEAdvertisment) ManufacturerData() []byte {
	return adv.manufacturerData
}
func (adv stubBLEAdvertisment) RSSI() int {
	return -50
}
func (adv stubBLEAdvertisment) Services() []ble.UUID {
	return nil
}
func (adv stubBLEAdvertisment) ServiceData() []ble.ServiceData {
	return nil
}
func (adv stubBLEAdvertisment) OverflowService() []ble.UUID {
	return nil
}
func (adv stubBLEAdvertisment) TxPowerLevel() int {
	return 0
}

func (adv stubBLEAdvertisment) SolicitedService() []ble.UUID {
	return nil
}
func (adv stubBLEAdvertisment) Connectable() bool {
	return true
}

func fromEnvironmentalAdvertisment(adv EnvironmentalAdvertisment) ble.Advertisement {
	mdata := []byte{0xff, 0xff}
	mdata = adv.data.MarshalBinary(mdata)
	return stubBLEAdvertisment{
		address:          adv.address,
		manufacturerData: mdata,
	}
}

func (s *CollectorSuite) expectScanLoop() {
	errs := make(chan error)
	s.bleAdvertisment = make(chan TimedAdvertisement)
	s.scanner.EXPECT().ScanLoop(mock.Anything, mock.Anything).
		RunAndReturn(
			func(ctx context.Context,
				filter ble.AdvFilter) (<-chan TimedAdvertisement, <-chan error, error) {
				go func() {
					<-ctx.Done()
					errs <- nil
					close(errs)
					close(s.bleAdvertisment)
				}()
				return s.bleAdvertisment, errs, nil
			})

}

func (s *CollectorSuite) sendEnvironmentalAdvertisment(receivedAt time.Time, adv EnvironmentalAdvertisment) {
	s.bleAdvertisment <- TimedAdvertisement{
		ReceivedAt: receivedAt,
		Adv:        fromEnvironmentalAdvertisment(adv),
	}
}

func (s *CollectorSuite) SetupTest() {
	s.journal = NewMockDataJournal(s.T())
	s.scanner = NewMockBLEScanner(s.T())
	s.cron = NewMockCronScheduler(s.T())
	s.environmentalOperator = newMockenvironmentalOperator(s.T())

	s.journal.EXPECT().GetActiveAssignments(mock.Anything).Return([]SensorAssignement{}, nil).Once()

	var err error

	s.collector, err = NewCollector(
		NewCollectorConfig(
			withJournal(s.journal),
			withScanner(s.scanner),
			withCron(s.cron),
			withEnvironmentalOperator(s.environmentalOperator),
		),
	)
	s.Require().NoError(err)

	s.ctx, s.cancel = context.WithTimeout(context.Background(), 500*time.Millisecond)

	s.collectError = make(chan error)
	s.expectScanLoop()
	s.cronTick = make(chan time.Time)
	s.cron.EXPECT().ScheduleLoop(mock.Anything, HourOfDay{Hour: 1, Minute: 42}, mock.Anything).Run(
		func(ctx context.Context, hod HourOfDay, fn func(context.Context, time.Time)) {
			for {
				select {
				case <-ctx.Done():
					return
				case t, ok := <-s.cronTick:
					slog.Info("ticking",
						slog.Time("time", t),
					)
					if ok == false {
						return
					}
					fn(ctx, t)
				}
			}
		})
	go func() {
		defer close(s.collectError)

		s.collectError <- s.collector.Collect(s.ctx)
	}()

}

func (s *CollectorSuite) TearDownTest() {
	s.cancel()
	err := <-s.collectError
	s.Require().NoError(err)
	_, ok := <-s.collectError
	s.Assert().Equal(false, ok)
}

func (s *CollectorSuite) TestEmpty() {}

func (s *CollectorSuite) TestDuplicates() {
	t := time.Now().Round(time.Second)
	adv := EnvironmentalAdvertisment{
		address: ble.NewAddr("02:02:02:02:02:02"),
		data: arisble.AdvertisementData{
			Location: arisble.Location{HiveID: 1, Placement: arisble.PlacementGeneral},
			CurrentPoint: arisble.DataPoint{
				Timestamp: arisble.Timestamp(t.Unix()),
			},
		},
	}

	s.journal.EXPECT().GetLocationAssignements(mock.Anything, "hive_001_general").Return(nil, nil).Once()
	s.journal.EXPECT().SaveEnvironmentalReadings(mock.Anything, mock.Anything).Return(nil).Once()
	subscription := s.collector.Subscribe(2)
	s.Require().NotNil(subscription)
	var wg sync.WaitGroup
	wg.Go(func() {
		s.sendEnvironmentalAdvertisment(t, adv)
		s.sendEnvironmentalAdvertisment(t.Add(100*time.Millisecond), adv)
	})

	received, ok := <-subscription

	s.Require().True(ok)
	s.Assert().Equal(adv.address.String(), received.Address)
	s.Assert().Equal(t, received.Current.Timestamp)
	s.Assert().Equal(t, received.LastSeen)

	wg.Wait()

	s.collector.Unsubscribe(subscription)

	_, ok = <-subscription
	s.Assert().False(ok)

}

func (s *CollectorSuite) TestSynchronizeDevice() {
	t := time.Now().Round(time.Second)
	receivedAt := t.Add(s.collector.config.MaximalTimeOffset + 1)
	adv := EnvironmentalAdvertisment{
		address: ble.NewAddr("02:02:02:02:02:02"),
		data: arisble.AdvertisementData{
			Location: arisble.Location{HiveID: 1, Placement: arisble.PlacementGeneral},
			CurrentPoint: arisble.DataPoint{
				Timestamp: arisble.Timestamp(t.Unix()),
			},
		},
	}

	s.journal.EXPECT().GetLocationAssignements(mock.Anything, "hive_001_general").Return(nil, nil).Once()
	s.journal.EXPECT().SaveEnvironmentalReadings(mock.Anything, mock.Anything).Return(nil).Once()
	s.scanner.EXPECT().Schedule(mock.Anything).RunAndReturn(func(task BLETask) error {
		task(context.Background(), nil)
		return nil
	})
	s.environmentalOperator.EXPECT().SynchronizeDevice(mock.Anything, nil, adv.address).Return(nil).Once()

	subscription := s.collector.Subscribe(1)

	var wg sync.WaitGroup
	wg.Go(func() {
		s.sendEnvironmentalAdvertisment(receivedAt, adv)
	})
	wg.Wait()

	received, ok := <-subscription
	s.Require().True(ok)
	s.Assert().Equal(adv.address.String(), received.Address)
	s.Assert().Equal(t, received.Current.Timestamp)
	s.Assert().Equal(receivedAt, received.LastSeen)

}

func (s *CollectorSuite) TestJanitor() {
	t := time.Date(2026, 1, 1, 1, 41, 0, 0, time.Local)
	late := t.Add(-s.collector.config.ActiveThresholdDuration - 1)
	s.collector.config.ConnectionJitter = 10 * time.Millisecond
	advs := []EnvironmentalAdvertisment{
		{
			address:    ble.NewAddr("02:02:02:02:02:02"),
			receivedAt: t,
			data: arisble.AdvertisementData{
				Location: arisble.Location{HiveID: 1, Placement: arisble.PlacementGeneral},
				CurrentPoint: arisble.DataPoint{
					Timestamp: arisble.Timestamp(t.Unix()),
				},
			},
		},
		{
			address:    ble.NewAddr("02:02:02:02:02:03"),
			receivedAt: t,
			data: arisble.AdvertisementData{
				Location: arisble.Location{HiveID: 2, Placement: arisble.PlacementGeneral},
				Memory:   244,
				CurrentPoint: arisble.DataPoint{
					Timestamp: arisble.Timestamp(t.Unix()),
				},
			},
		},
		{
			address:    ble.NewAddr("02:02:02:02:02:04"),
			receivedAt: late,
			data: arisble.AdvertisementData{
				Location: arisble.Location{HiveID: 3, Placement: arisble.PlacementGeneral},
				Memory:   244,
				CurrentPoint: arisble.DataPoint{
					Timestamp: arisble.Timestamp(late.Unix()),
				},
			},
		},
	}

	s.journal.EXPECT().GetLocationAssignements(mock.Anything, "hive_001_general").Return(nil, nil)
	s.journal.EXPECT().GetLocationAssignements(mock.Anything, "hive_002_general").Return(nil, nil)
	s.journal.EXPECT().GetLocationAssignements(mock.Anything, "hive_003_general").Return(nil, nil)
	s.journal.EXPECT().SaveEnvironmentalReadings(mock.Anything, mock.Anything).Return(nil)

	subscription := s.collector.Subscribe(len(advs))

	var wg sync.WaitGroup
	wg.Go(func() {
		for _, adv := range advs {
			s.sendEnvironmentalAdvertisment(adv.receivedAt, adv)
		}
	})
	wg.Wait()
	for _, adv := range advs {
		received, ok := <-subscription
		s.Require().True(ok)
		s.Assert().Equal(adv.address.String(), received.Address)
		s.Assert().Equal(adv.data.CurrentPoint.Timestamp.ToTime(), received.Current.Timestamp)
		s.Assert().Equal(adv.receivedAt, received.LastSeen)
	}

	s.scanner.EXPECT().Schedule(mock.Anything).RunAndReturn(func(task BLETask) error {
		task(context.Background(), nil)
		return nil
	})
	wg.Add(3)
	s.environmentalOperator.EXPECT().
		SynchronizeDevice(mock.Anything, nil, advs[0].address).
		RunAndReturn(func(ctx context.Context, dev BLEDevice, address ble.Addr) error {
			wg.Done()
			return nil
		}).Once()

	s.environmentalOperator.EXPECT().
		SynchronizeDevice(mock.Anything, nil, advs[1].address).
		RunAndReturn(func(ctx context.Context, dev BLEDevice, address ble.Addr) error {
			wg.Done()
			return nil
		}).Once()
	s.environmentalOperator.EXPECT().
		ReadJournalAndEraseDevice(mock.Anything, nil, advs[1].address, "hive_002_general", mock.Anything).
		RunAndReturn(func(ctx context.Context, dev BLEDevice, address ble.Addr, locationID string, journal DataJournal) error {
			wg.Done()
			return nil
		}).Once()

	s.cronTick <- t.Add(1 * time.Minute)

	wg.Wait()
}

func TestCollectorSuite(t *testing.T) {
	suite.Run(t, new(CollectorSuite))
}
