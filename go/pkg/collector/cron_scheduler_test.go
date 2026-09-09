package collector

import (
	"context"
	"testing"
	"time"

	"github.com/stretchr/testify/suite"
)

type CronSchedulerSuite struct {
	suite.Suite
	clock     *MockClock
	scheduler CronScheduler
}

func (s *CronSchedulerSuite) SetupTest() {
	s.clock = NewMockClock(s.T())
	s.scheduler = NewCronScheduler(s.clock)
}

func (s *CronSchedulerSuite) TearDownTest() {
	s.clock.AssertExpectations(s.T())
}

func (s *CronSchedulerSuite) TestScheduling() {
	t := time.Date(2026, 01, 01, 23, 59, 0, 0, time.Local)
	s.clock.EXPECT().Now().Return(t).Once()
	s.clock.EXPECT().Now().Return(t.Add(time.Minute + time.Second)).Once()
	s.clock.EXPECT().NewTimer(time.Minute).Return(time.NewTimer(time.Millisecond)).Once()
	s.clock.EXPECT().NewTimer(24*time.Hour - time.Second).Return(time.NewTimer(24 * time.Hour)).Once()

	called := make(chan struct{})
	ctx, cancel := context.WithTimeout(context.Background(), 20*time.Millisecond)
	defer cancel()
	s.scheduler.ScheduleLoop(ctx, HourOfDay{Hour: 0, Minute: 0}, func(time.Time) { close(called) })
	ok := true
	select {
	case _, ok = <-called:
	default:
	}
	s.Assert().False(ok)

}

func TestCronSchedulerSuite(t *testing.T) {
	suite.Run(t, new(CronSchedulerSuite))
}
