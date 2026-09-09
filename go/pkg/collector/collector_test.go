package collector

import (
	"context"
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
	journal         *MockDataJournal
	scanner         *MockBLEScanner
	cron            *MockCronScheduler
	collector       *Collector
	bleAdvertisment chan TimedAdvertisement
	collectError    chan error
	ctx             context.Context
	cancel          context.CancelFunc
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
	s.journal.EXPECT().GetActiveAssignments(mock.Anything).Return([]SensorAssignement{}, nil).Once()

	var err error

	s.collector, err = NewCollector(
		NewCollectorConfig(
			withJournal(s.journal),
			withScanner(s.scanner),
			withCron(s.cron),
		),
	)
	s.Require().NoError(err)

	s.ctx, s.cancel = context.WithTimeout(context.Background(), 500*time.Millisecond)

	s.collectError = make(chan error)
	s.expectScanLoop()
	s.cron.EXPECT().ScheduleLoop(mock.Anything, HourOfDay{Hour: 1, Minute: 42}, mock.Anything)
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
	s.journal.EXPECT().SaveEnvironmentalReadings(mock.Anything, mock.Anything).Return(nil)
	subscription := s.collector.Subscribe()
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

func TestCollectorSuite(t *testing.T) {
	suite.Run(t, new(CollectorSuite))
}
