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
	journal         *MockDataJournal
	device          *mockbleDevice
	collector       *Collector
	bleAdvertisment chan ble.Advertisement
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

func fromEnvironmentalAdvertisment(adv EnvAdvertisment) ble.Advertisement {
	mdata := []byte{0xff, 0xff}
	mdata = adv.data.MarshalBinary(mdata)
	return stubBLEAdvertisment{
		address:          adv.address,
		manufacturerData: mdata,
	}
}

func (s *CollectorSuite) expectScanLoop() {
	s.device.EXPECT().Scan(mock.Anything, mock.Anything, mock.Anything).
		RunAndReturn(
			func(ctx context.Context,
				allowDuplicates bool,
				handler ble.AdvHandler) error {
				for {
					select {
					case <-ctx.Done():
						return ctx.Err()
					case adv := <-s.bleAdvertisment:
						slog.Info("received",
							slog.String("address", adv.Addr().String()))
						handler(adv)
					}
				}
			})

}

func (s *CollectorSuite) sendEnvironmentalAdvertisment(adv EnvAdvertisment) {
	s.bleAdvertisment <- fromEnvironmentalAdvertisment(adv)

}

func (s *CollectorSuite) SetupTest() {
	s.bleAdvertisment = make(chan ble.Advertisement)
	s.journal = NewMockDataJournal(s.T())
	s.device = newMockbleDevice(s.T())
	s.journal.EXPECT().GetActiveAssignments(mock.Anything).Return([]SensorAssignement{}, nil).Once()

	var err error

	s.collector, err = NewCollector(nil, s.journal)
	s.Require().NoError(err)

	s.ctx, s.cancel = context.WithTimeout(context.Background(), 500*time.Millisecond)

	s.collectError = make(chan error)
	s.expectScanLoop()
	go func() {
		defer close(s.collectError)

		s.collectError <- s.collector.Collect(s.ctx, s.device)

	}()

}

func (s *CollectorSuite) TearDownTest() {
	s.cancel()
	err := <-s.collectError
	s.Require().NoError(err)
	_, ok := <-s.collectError
	s.Assert().Equal(false, ok)

	s.journal.AssertExpectations(s.T())
	s.device.AssertExpectations(s.T())
}

func (s *CollectorSuite) TestEmpty() {}

func (s *CollectorSuite) TestDuplicates() {
	t := time.Now().Round(time.Second)
	adv := EnvAdvertisment{
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
		s.sendEnvironmentalAdvertisment(adv)
		s.sendEnvironmentalAdvertisment(adv)
	})

	received, ok := <-subscription

	s.Require().True(ok)
	s.Assert().Equal(adv.address.String(), received.Address)
	s.Assert().Equal(adv.data.CurrentPoint.Timestamp.ToTime(), received.Current.Timestamp)

	wg.Wait()

	s.collector.Unsubscribe(subscription)

	_, ok = <-subscription
	s.Assert().False(ok)

}

func TestCollectorSuite(t *testing.T) {
	suite.Run(t, new(CollectorSuite))
}
