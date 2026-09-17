package main

import (
	"strings"
	"testing"

	"github.com/jessevdk/go-flags"
	"github.com/stretchr/testify/assert"
)

func TestLUTParsing(t *testing.T) {
	testdata := []struct {
		name, input, error string
		expected           LUTPoint
	}{
		{name: "empty", input: "  ", error: "empty"},
		{name: "missingEqual", input: "12", error: "missing '=' character"},
		{name: "multipleEqual", input: "12=10.0=22", error: "multiple '=' characters"},
		{name: "outOfRangeTemp", input: "128=10.0", error: "out of range"},
		{name: "invalidRPM", input: "127=-asd", error: "invalid syntax"},
		{name: "noInfinity", input: "127=-inf", error: "infinite speed not allowed"},
		{name: "noInfinity", input: "127=NaN", error: "NaN not allowed"},
	}

	for _, d := range testdata {
		t.Run(d.name, func(t *testing.T) {
			assert := assert.New(t)
			var pt LUTPoint
			err := pt.UnmarshalFlag(d.input)
			if len(d.error) == 0 {
				assert.NoError(err)
				assert.Equal(d.expected, pt)
			} else {
				assert.ErrorContains(err, d.error)
				assert.Equal(LUTPoint{}, pt)
			}
		})
	}

}

func TestMultipleDefault(t *testing.T) {
	type TestOptions struct {
		Points []LUTPoint `short:"c" default:"-128=15.0" default:"10=15.0"`
	}

	testdata := []struct {
		name, input string
		expected    []LUTPoint
	}{
		{name: "empty", input: "", expected: []LUTPoint{{-128, 15.0}, {10, 15.0}}},
		{name: "redefines", input: "-c 10=12.0", expected: []LUTPoint{{10, 12.0}}},
		{name: "multiples", input: "-c 0=0.0 -c 10=0.0 -c 11=15.0 -c 55=100.0", expected: []LUTPoint{{0, 0.0}, {10, 0.0}, {11, 15.0}, {55, 100.0}}},
	}

	for _, d := range testdata {
		t.Run(d.name, func(t *testing.T) {
			assert := assert.New(t)
			var opts TestOptions
			inputArgs := []string{"test"}
			for _, a := range strings.Split(d.input, " ") {
				if len(a) > 0 {
					inputArgs = append(inputArgs, a)
				}
			}

			args, err := flags.ParseArgs(&opts, inputArgs)

			assert.NoError(err)
			assert.Equal(d.expected, opts.Points)
			assert.Equal([]string{"test"}, args)
		})
	}

}
