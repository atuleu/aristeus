package collector

import (
	"context"
	"errors"
	"log/slog"
	"sync"
	"time"

	"github.com/atuleu/aristeus/go/pkg/arisble"
	"github.com/go-ble/ble"
)

type BLEDevice interface {
	arisble.BLEDialer
	Scan(ctx context.Context, allowDuplicates bool, handler ble.AdvHandler) error
}

type BLETask func(ctx context.Context, dev BLEDevice)

type BLEScanner interface {
	ScanLoop(ctx context.Context, filter ble.AdvFilter) (<-chan ble.Advertisement, <-chan error, error)
	Schedule(task BLETask) error
}

type bleScanner struct {
	device BLEDevice
	tasks  chan BLETask
	mx     sync.RWMutex
}

func NewScanner(dev BLEDevice) BLEScanner {
	return &bleScanner{
		device: dev,
	}
}

func (s *bleScanner) ScanLoop(ctx context.Context, filter ble.AdvFilter) (<-chan ble.Advertisement, <-chan error, error) {
	s.mx.Lock()
	defer s.mx.Unlock()

	if s.tasks != nil {
		return nil, nil, errors.New("already started")
	}
	s.tasks = make(chan BLETask, 64)
	advertisments := make(chan ble.Advertisement, 64)
	errors := make(chan error, 1)
	go s.scanLoop(ctx, filter, advertisments, errors)

	return advertisments, errors, nil
}

func (s *bleScanner) scanLoop(ctx context.Context, filter ble.AdvFilter, advertisments chan ble.Advertisement, errs chan error) {
	logger := slog.With("module", "BLEScanLoop")

	defer func() {
		close(advertisments)
		logger.Debug("closed advertisments")
	}()
	defer func() {
		close(errs)
		logger.Debug("closed errs")
	}()
	defer func() {
		s.mx.Lock()
		defer s.mx.Unlock()
		close(s.tasks)
		s.tasks = nil
		logger.Debug("closed tasks")
	}()

	onAdv := func(adv ble.Advertisement) {
		if filter(adv) == false {
			return
		}
		select {
		case advertisments <- adv:
			//good
		default:
			logger.Error("dropping advertisment due to overflow",
				slog.String("address", adv.Addr().String()),
			)
		}
	}

	startBLEScanning := func() (context.CancelFunc, <-chan error) {
		logger.Debug("scanning")
		errors := make(chan error, 1)
		scanContext, cancelContext := context.WithCancel(ctx)
		go func() {
			errors <- s.device.Scan(scanContext, true, onAdv)
			close(errors)
		}()
		return cancelContext, errors
	}

	cancelScan, scanErrors := startBLEScanning()

	for {
		select {
		case <-ctx.Done():
			logger.Info("done")
			cancelScan()
			// we poll has we could race for cancellation, but the channel could
			// already be nil
			var err error
			if scanErrors != nil {
				err, _ = <-scanErrors

			}

			if errors.Is(err, context.Canceled) {
				err = nil
			}

			errs <- err

			return
		case err, ok := <-scanErrors:
			if ok == false {
				scanErrors = nil
				continue
			}
			if err == nil || errors.Is(err, context.Canceled) {
				// cancel race condition, where the other routine push before we
				// see the cancellation ourselves , nothing to do
				continue
			}
			logger.Error("scan error", slog.String("error", err.Error()))
			errs <- err
			return

		case task := <-s.tasks:
			logger.Debug("received task")
			cancelScan()
			err, ok := <-scanErrors
			if ok == true && err != nil && errors.Is(err, context.Canceled) == false {
				errs <- err
				return
			}

			// drain all the tasks currently queued.
			for hasTask := true; hasTask; {
				withTimeout, cancelTimeout := context.WithTimeout(ctx, 5*time.Minute)
				task(withTimeout, s.device)
				cancelTimeout()
				select {
				case task = <-s.tasks:
				default:
					hasTask = false
				}
			}

			cancelScan, scanErrors = startBLEScanning()
		}
	}
}

func (s *bleScanner) Schedule(task BLETask) error {
	s.mx.RLock()
	defer s.mx.RUnlock()
	if s.tasks == nil {
		return errors.New("not started")
	}
	s.tasks <- task
	return nil
}
