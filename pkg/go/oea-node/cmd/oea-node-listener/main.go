package main

import (
	"fmt"
	"log/slog"
	"os"
)

func ErrAttr(err error) slog.Attr {
	return slog.Any("error", err)
}

func main() {
	if err := execute(); err != nil {
		slog.Error("unhandled error", ErrAttr(err))
		os.Exit(1)
	}
}

func execute() error {
	return fmt.Errorf("not yet implemented")
}
