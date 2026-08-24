package main

import (
	"log/slog"
	"os"

	"github.com/jessevdk/go-flags"
)

type Options struct {
}

var opts = &Options{}
var parser = flags.NewParser(opts, flags.Default)

func main() {
	if err := execute(); err != nil {
		slog.Error("Unhandled error", slog.String("error", err.Error()))
		os.Exit(1)
	}
}

func execute() error {
	_, err := parser.Parse()
	if err != nil && flags.WroteHelp(err) == true {
		return nil
	}
	return err
}
