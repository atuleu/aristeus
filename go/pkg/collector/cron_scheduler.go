package collector

import (
	"context"
	"time"
)

type HourOfDay struct {
	Hour   int
	Minute int
}

type Clock interface {
	Now() time.Time
	NewTimer(d time.Duration) *time.Timer
}

type timeClock struct{}

func (t timeClock) Now() time.Time {
	return time.Now()
}
func (t timeClock) NewTimer(d time.Duration) *time.Timer {
	return time.NewTimer(d)
}

type CronScheduler interface {
	ScheduleLoop(ctx context.Context, hod HourOfDay, fn func(time.Time))
}

type cronScheduler struct {
	clock Clock
}

func NewCronScheduler(clock Clock) CronScheduler {
	if clock == nil {
		clock = timeClock{}
	}
	return cronScheduler{clock: clock}
}

func (s cronScheduler) nextTimer(hod HourOfDay) *time.Timer {
	now := s.clock.Now()
	next := time.Date(now.Year(), now.Month(), now.Day(), hod.Hour, hod.Minute, 0, 0, now.Location()).Add(24 * time.Hour)
	return s.clock.NewTimer(next.Sub(now))
}

func (s cronScheduler) ScheduleLoop(ctx context.Context, hod HourOfDay, fn func(time.Time)) {
	next := s.nextTimer(hod)
	for {
		select {
		case <-ctx.Done():
			next.Stop()
			return
		case now := <-next.C:
			fn(now)
			next = s.nextTimer(hod)
		}
	}
}
