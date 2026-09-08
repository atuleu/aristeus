package main

import (
	"context"
	"log/slog"
	"os"
	"testing"
	"time"

	"github.com/atuleu/aristeus/go/internal/blemock"
	"github.com/go-ble/ble"
	"github.com/stretchr/testify/mock"
	"github.com/stretchr/testify/suite"
)

type BLEScannerSuite struct {
	suite.Suite
	device           *MockBLEDevice
	scanner          BLEScanner
	ctx              context.Context
	cancel           context.CancelFunc
	stubAdvertisment chan ble.Advertisement
}

func (s *BLEScannerSuite) stubScan(ctx context.Context, allowDuplicates bool, handler ble.AdvHandler) error {
	for {
		select {
		case <-ctx.Done():
			return ctx.Err()
		case adv, ok := <-s.stubAdvertisment:
			if ok == false {
				s.stubAdvertisment = nil
				continue
			}
			handler(adv)
		}
	}
}

func (s *BLEScannerSuite) SetupTest() {
	s.device = NewMockBLEDevice(s.T())
	s.scanner = NewScanner(s.device)
	s.ctx, s.cancel = context.WithTimeout(context.Background(), 500*time.Millisecond)
	s.stubAdvertisment = make(chan ble.Advertisement, 10)
}

func (s *BLEScannerSuite) TearDownTest() {
	s.device.AssertExpectations(s.T())
	s.cancel()
	close(s.stubAdvertisment)
}

func TestBLEScannerSuite(t *testing.T) {
	suite.Run(t, new(BLEScannerSuite))
}

func (s *BLEScannerSuite) TestReturnNil() {
	filterAny := func(ble.Advertisement) bool { return true }

	s.device.EXPECT().Scan(mock.Anything, true, mock.Anything).RunAndReturn(s.stubScan).Once()
	ctx, cancel := context.WithCancel(s.ctx)
	defer cancel()
	advs, errs, err := s.scanner.ScanLoop(ctx, filterAny)

	if s.Assert().NoError(err) == false {
		return
	}
	cancel()

	_, ok := <-advs
	s.Assert().False(ok)

	err, ok = <-errs
	s.Assert().True(ok)
	s.Assert().NoError(err)

	err, ok = <-errs
	s.Assert().False(ok)
	s.Assert().NoError(err)
}

func (s *BLEScannerSuite) TestPauseOnlyOnce() {
	filterAny := func(ble.Advertisement) bool { return true }

	s.device.EXPECT().Scan(mock.Anything, true, mock.Anything).RunAndReturn(s.stubScan).Once()
	s.device.EXPECT().Scan(mock.Anything, true, mock.Anything).RunAndReturn(s.stubScan).Once()

	ctx, cancel := context.WithCancel(s.ctx)
	defer cancel()
	advs, errs, err := s.scanner.ScanLoop(ctx, filterAny)

	if s.Assert().NoError(err) == false {
		return
	}

	done := make(chan struct{})
	s.scanner.Schedule(func(ctx context.Context, dev BLEDevice) {
		time.Sleep(10 * time.Millisecond)
	})
	s.scanner.Schedule(func(ctx context.Context, dev BLEDevice) {
		close(done)
	})

	<-done
	cancel()

	_, ok := <-advs
	s.Assert().False(ok)

	err, ok = <-errs
	s.Assert().True(ok)
	s.Assert().NoError(err)

	err, ok = <-errs
	s.Assert().False(ok)
	s.Assert().NoError(err)
}

func (s *BLEScannerSuite) newAdvertisement(addr string) ble.Advertisement {
	adv := blemock.NewMockAdvertisement(s.T())
	adv.EXPECT().Addr().Return(ble.NewAddr(addr))
	return adv
}

func (s *BLEScannerSuite) TestAdvertisment() {
	filter := func(adv ble.Advertisement) bool {
		if adv.Addr().String() == "02:02:02:02:02:02" {
			return true
		}
		return false
	}

	s.device.EXPECT().Scan(mock.Anything, true, mock.Anything).RunAndReturn(s.stubScan).Once()

	ctx, cancel := context.WithCancel(s.ctx)
	defer cancel()
	advs, errs, err := s.scanner.ScanLoop(ctx, filter)

	if s.Assert().NoError(err) == false {
		return
	}

	s.stubAdvertisment <- s.newAdvertisement("02:02:02:02:02:02")
	s.stubAdvertisment <- s.newAdvertisement("02:02:02:02:02:01")
	s.stubAdvertisment <- s.newAdvertisement("02:02:02:02:02:02")

	adv, ok := <-advs
	if s.Assert().True(ok) == true {
		s.Assert().Equal("02:02:02:02:02:02", adv.Addr().String())
	}
	adv, ok = <-advs
	if s.Assert().True(ok) == true {
		s.Assert().Equal("02:02:02:02:02:02", adv.Addr().String())
	}

	cancel()

	_, ok = <-advs
	s.Assert().False(ok)

	err, ok = <-errs
	s.Assert().True(ok)
	s.Assert().NoError(err)

	err, ok = <-errs
	s.Assert().False(ok)
	s.Assert().NoError(err)
}

func init() {
	opts := &slog.HandlerOptions{
		Level: slog.LevelDebug,
	}
	logger := slog.New(slog.NewTextHandler(os.Stdout, opts))
	slog.SetDefault(logger)
}
