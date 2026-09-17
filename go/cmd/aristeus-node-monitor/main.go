package main

import (
	"os"

	"github.com/jessevdk/go-flags"
)

type Options struct {
}

var opts = Options{}

var parser = flags.NewParser(&opts, flags.Default)

func main() {
	if _, err := parser.Parse(); err != nil {
		if flags.WroteHelp(err) == true {
			os.Exit(0)
		}
		os.Exit(1)
	}
}
