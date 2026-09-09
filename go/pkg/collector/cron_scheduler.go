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
	NewTimer(time.Duration) *time.Timer
}

type timeClock struct{}

func (t timeClock) Now() time.Time {
	return time.Now()
}
func (t timeClock) NewTimer(d time.Duration) *time.Timer {
	return time.NewTimer(d)
}

func CronSchedule(ctx context.Context, clock Clock, hod HourOfDay, fn func()) {
	if clock == nil {
		clock = timeClock{}
	}

}
