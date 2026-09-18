package main

import (
	"context"
	"errors"
	"fmt"
	"log/slog"
	"net/http"
	"os"
	"os/signal"
	"sync"
	"time"

	"github.com/jessevdk/go-flags"
	"github.com/lmittmann/tint"
	"github.com/prometheus/client_golang/prometheus"
	"github.com/prometheus/client_golang/prometheus/collectors"
	"github.com/prometheus/client_golang/prometheus/promhttp"
	"golang.org/x/term"
)

func main() {

	if err := execute(); err != nil {
		slog.Error("unhandled error", slog.String("error", err.Error()))
		os.Exit(1)
	}
}

func servePrometheus(ctx context.Context, reg *prometheus.Registry, address string) error {
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

func setupLogger() {
	isTerminal := term.IsTerminal(int(os.Stderr.Fd()))
	var level slog.Level
	switch len(opts.Verbose) {
	case 0:
		level = slog.LevelWarn
	case 1:
		level = slog.LevelInfo
	case 2:
		level = slog.LevelDebug
	}
	options := &tint.Options{
		NoColor: !isTerminal,
		Level:   level,
		ReplaceAttr: func(groups []string, a slog.Attr) slog.Attr {
			switch a.Value.Kind() {
			case slog.KindAny:
				if _, ok := a.Value.Any().(error); ok {
					return tint.Attr(9, a)
				}
				return a

			case slog.KindString:
				switch a.Key {
				case "error":
					return tint.Attr(9, a)
				case "module":
					return tint.Attr(4, a)
				default:
					return a
				}
			default:
				return a
			}
		},
	}

	handler := tint.NewTextHandler(os.Stderr, options)
	logger := slog.New(handler)
	slog.SetDefault(logger)
	slog.SetLogLoggerLevel(slog.LevelInfo)

}

func execute() error {

	if _, err := parser.Parse(); err != nil {
		if flags.WroteHelp(err) == true {
			return nil
		}
		return err
	}

	setupLogger()

	reg := prometheus.NewRegistry()

	err := reg.Register(collectors.NewBuildInfoCollector())
	if err != nil {
		return fmt.Errorf("could not register default collectors: %w", err)
	}

	ctx, stop := signal.NotifyContext(context.Background(), os.Interrupt)
	defer stop()

	var wg sync.WaitGroup
	errs := make(chan error)
	addTask := func(fn func() error) {
		wg.Go(func() {
			err := fn()
			if err != nil {
				errs <- err
			}
		})
	}
	addTask(func() error { return servePrometheus(ctx, reg, opts.PrometheusAddress) })
	addTask(func() error { return emc2101Loop(ctx, reg) })

	if opts.JetsonUsage {
		addTask(func() error { return jestonUsageLoop(ctx, reg) })
	}
	if opts.RPIUsage {
		addTask(func() error { return rpiUsageLoop(ctx, reg) })
	}

	var allErrs []error
	select {
	case err := <-errs:
		allErrs = append(allErrs, err)
		stop()
	case <-ctx.Done():
		break
	}

	wg.Wait()
	close(errs)
	for err := range errs {
		allErrs = append(allErrs, err)
	}

	return errors.Join(allErrs...)
}
