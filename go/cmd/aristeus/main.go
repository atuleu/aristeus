package main

import (
	"fmt"
	"log/slog"
	"os"
)

func main() {
	if err := execute(); err != nil {
		slog.Error("Unhandled error", slog.String("error", err.Error()))
		os.Exit(1)
	}
}

func execute() error {
	return fmt.Errorf("Not yet implemented.")
}
