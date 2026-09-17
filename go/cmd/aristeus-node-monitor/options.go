package main

import (
	"errors"
	"math"
	"strconv"
	"strings"
	"time"

	"github.com/atuleu/aristeus/go/pkg/emc2101"
	"github.com/jessevdk/go-flags"
)

type LUTPoint emc2101.LUTPoint

func (p *LUTPoint) UnmarshalFlag(value string) error {
	value = strings.TrimSpace(value)
	if len(value) == 0 {
		return errors.New("empty")
	}
	parts := strings.Split(value, "=")
	if len(parts) < 2 {
		return errors.New("missing '=' character")
	}
	if len(parts) > 2 {
		return errors.New("multiple '=' characters")
	}

	temp, err := strconv.ParseInt(parts[0], 10, 8)
	if err != nil {
		return err
	}
	power, err := strconv.ParseFloat(parts[1], 64)
	if err != nil {
		return err
	}
	if math.IsInf(power, 1) || math.IsInf(power, -1) {
		return errors.New("infinite speed not allowed")
	}

	if math.IsNaN(power) {
		return errors.New("NaN not allowed")
	}

	p.Temperature = int8(temp)
	p.PWM = min(max(power, 0.0), 100.0)
	return nil
}

type Options struct {
	I2CBus            string        `long:"i2c-bus" description:"i2c-bus to use, empty use the first one" default:""`
	PrometheusAddress string        `long:"prometheus-address" description:"prometheus address to serve" default:":2112"`
	ScanPeriod        time.Duration `long:"period" description:"period for update" default:"5s"`
	LUTPoint          []LUTPoint    `long:"curve" short:"c" description:"curve to control the temperature" default:"-128=15.0" default:"15=15.0" default:"55=100.0"`
}

var opts = Options{}

var parser = flags.NewParser(&opts, flags.Default)
