package main

import (
	"context"
	"log/slog"
	"net/http"
	"os"
	"os/signal"
	"time"

	"github.com/jessevdk/go-flags"
	"github.com/prometheus/client_golang/prometheus"
	"github.com/prometheus/client_golang/prometheus/promhttp"
)

type Options struct {
	I2CBus            string `long:"i2c-bus" description:"i2c-bus to use" default:"/dev/i2c-0"`
	PrometheusAddress string `long:"prometheus-address" description:"prometheus address to serve" default:":2112"`
}

var opts = Options{}

var parser = flags.NewParser(&opts, flags.Default)

func main() {

	if err := execute(); err != nil {
		slog.Error("unhandled error", slog.String("error", err.Error()))
		os.Exit(1)
	}
}

func emc2101Loop(ctx context.Context, i2cbus string) {

}

func serverPrometheus(ctx context.Context, reg *prometheus.Registry, address string) error {
	mux := http.NewServeMux()

	mux.Handle("/metrics", promhttp.HandlerFor(reg, promhttp.HandlerOpts{}))

	server := http.Server{
		Addr:    address,
		Handler: mux,
	}

	logger := slog.With(slog.String("module", "prometheus"), slog.String("address", address))

	go func() {
		<-ctx.Done()

		timeoutCtx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
		defer cancel()
		err := server.Shutdown(timeoutCtx)
		if err != nil {
			logger.Error("could not shutdown listener",
				slog.String("error", err.Error()))
			return
		}
		logger.Info("graceful shutdown")

	}()

	logger.Info("listening")
	if err := server.ListenAndServe(); err != nil && err != http.ErrServerClosed {
		logger.Error("server failure", slog.String("error", err.Error()))
		return err
	}
	return nil
}

func execute() error {

	if _, err := parser.Parse(); err != nil {
		if flags.WroteHelp(err) == true {
			return nil
		}
		return err
	}

	reg := prometheus.NewRegistry()

	ctx, stop := signal.NotifyContext(context.Background(), os.Interrupt)
	defer stop()

	return serverPrometheus(ctx, reg, opts.PrometheusAddress)
}
