package collector

import (
	"context"
	"database/sql"
	"encoding/json"
	"errors"
	"fmt"
	"log/slog"
	"net/http"
	"strconv"
	"sync"
	"time"

	"github.com/atuleu/aristeus/go/pkg/sse"
)

type collectorHttpServer struct {
	logger    *slog.Logger
	collector *Collector
	ctx       context.Context
}

func (s collectorHttpServer) handleState(w http.ResponseWriter, r *http.Request) {
	logger := s.logger.With(
		slog.String("method", r.Method),
		slog.String("URI", r.RequestURI),
		slog.String("remote", r.RemoteAddr),
	)

	events := make(chan sse.ServerSideEvent, 16)

	subscription := s.collector.Subscribe(1)
	go func() {
		defer close(events)
		for {
			select {
			case <-s.ctx.Done():
				logger.Info("parent cancellation", slog.String("error", s.ctx.Err().Error()))
				return
			case d, ok := <-subscription:
				if ok == false {
					return
				}
				events <- sse.ServerSideEvent{Name: "environmental_device_update", Data: d}
			}
		}
	}()
	defer func() {
		s.collector.Unsubscribe(subscription)
		for range events {
			// drain all remaining events so subscription closing will be seen
			// by glue routine. It also waits for the glue routine to close!
		}
	}()

	err := sse.HandleSSE(w, r, events, time.Minute)
	if err != nil && err != sse.EOS && err != context.Canceled {
		logger.Warn("done",
			slog.String("error", err.Error()))
		return
	}

	logger.Info("done")
}

func extractLocationID(r *http.Request) (string, error) {
	locationID := r.PathValue("location_id")
	_, err := ParseLocationID(locationID)
	return locationID, err

}

func isNumericBytes(s string) bool {
	if len(s) == 0 {
		return false
	}
	for i := 0; i < len(s); i++ {
		if s[i] < '0' || s[i] > '9' {
			return false
		}
	}
	return true
}

func (s collectorHttpServer) handleEnvironmentalHistory(w http.ResponseWriter, r *http.Request) {
	logger := s.logger.With(
		slog.String("method", r.Method),
		slog.String("Path", r.URL.EscapedPath()),
		slog.String("remote", r.RemoteAddr),
	)

	locationID, err := extractLocationID(r)
	if err != nil {
		logger.Error("invalid input",
			slog.String("location_id", locationID),
			slog.String("error", err.Error()))
		http.Error(w, "invalid location_id", http.StatusBadRequest)
		return
	}

	ctx, cancel := context.WithTimeout(r.Context(), 5*time.Second)
	defer cancel()

	parseTimestamp := func(def time.Time, key string) (time.Time, error) {
		raw := r.URL.Query().Get(key)
		if raw == "" {
			return def, nil
		}
		if isNumericBytes(raw) == false {
			http.Error(w, "invalid "+key, http.StatusBadRequest)
			return def, fmt.Errorf("invalid %s", key)
		}
		ms, err := strconv.ParseInt(raw, 10, 64)
		if err != nil {
			http.Error(w, "invalid "+key+"="+raw, http.StatusBadRequest)
			return def, fmt.Errorf("invalid %s=%s: %w", key, raw, err)
		}
		return time.UnixMilli(ms), nil
	}
	now := time.Now()
	start, err := parseTimestamp(time.Date(now.Year(), 1, 1, 0, 0, 0, 0, time.Local), "since")
	if err != nil {
		logger.Error("could not parse 'since' query parameter",
			slog.String("error", err.Error()))
		return
	} else {
		logger = logger.With("since", r.URL.Query().Get("since"))
	}
	end, err := parseTimestamp(now, "until")
	if err != nil {
		logger.Error("could not parse 'until' query parameter",
			slog.String("error", err.Error()))
		return
	} else {
		logger = logger.With("until", r.URL.Query().Get("until"))
	}

	readings, err := s.collector.journal.GetEnvironmentalHistory(ctx, locationID, start, end)

	if err != nil && errors.Is(err, sql.ErrNoRows) == false {
		logger.Error("database error",
			slog.String("method", "GetEnvironmentalHistory"),
			slog.String("error", err.Error()),
		)
		http.Error(w, "internal server error", http.StatusInternalServerError)
		return
	}

	if errors.Is(err, sql.ErrNoRows) || len(readings) == 0 {
		http.Error(w, "no readings for location '"+locationID+"'", http.StatusNotFound)
		return
	}

	ts := EnvironmentalTimeSeries{
		LocationID:       locationID,
		Timestamp:        make(Timebase, 0, len(readings)),
		Temperature_C:    make(FloatVector, 0, len(readings)),
		Humidity_percent: make(FloatVector, 0, len(readings)),
		Pressure_hPa:     make(FloatVector, 0, len(readings)),
		CO2_PPM:          make(UintVector, 0, len(readings)),
	}

	for _, r := range readings {
		ts.Timestamp = append(ts.Timestamp, r.Timestamp)
		ts.Temperature_C = append(ts.Temperature_C, r.Temperature_C)
		ts.Humidity_percent = append(ts.Humidity_percent, r.Humidity_percent)
		ts.Pressure_hPa = append(ts.Pressure_hPa, r.Pressure_hPa)
		ts.CO2_PPM = append(ts.CO2_PPM, r.CO2_ppm)

	}

	payload, err := json.Marshal(&ts)

	if err != nil {
		logger.Error("could not encode timeseries",
			slog.String("error", err.Error()))
		http.Error(w, "internal server error", http.StatusInternalServerError)
		return
	}

	w.Header().Set("Content-Type", "application/json")
	w.WriteHeader(http.StatusOK)
	w.Write(payload)
}

func serveCollector(ctx context.Context, c *Collector) {
	if c == nil {
		panic("nil collector")
	}
	mux := http.NewServeMux()
	s := collectorHttpServer{
		collector: c,
		logger:    slog.With(slog.String("module", "HTTPServer")),
		ctx:       ctx,
	}

	mux.HandleFunc("GET /api/state", s.handleState)

	mux.HandleFunc("GET /api/environmental/{location_id}/history",
		s.handleEnvironmentalHistory)

	server := &http.Server{
		Addr:              ":3000",
		Handler:           mux,
		ReadHeaderTimeout: 1 * time.Second,
		ReadTimeout:       5 * time.Second,
		IdleTimeout:       2 * time.Minute,
	}

	var wg sync.WaitGroup

	stopListen := make(chan struct{})
	wg.Go(func() {
		select {
		case <-ctx.Done():
		case <-stopListen:
			return
		}
		s.logger.Info("shutdown signal received")
		shutdownCtx, shutdownCancel := context.WithTimeout(context.Background(), 5*time.Second)
		defer shutdownCancel()
		if err := server.Shutdown(shutdownCtx); err != nil {
			s.logger.Error("shutdown failure",
				slog.String("error", err.Error()))
		} else {
			s.logger.Info("graceful shutdown")
		}
	})
	defer wg.Wait()

	s.logger.Info("listening",
		slog.String("address", server.Addr))

	if err := server.ListenAndServe(); err != nil && err != http.ErrServerClosed {
		s.logger.Error("server error",
			slog.String("error", err.Error()))
	}

	close(stopListen)
}
