package collector

import (
	"context"
	"database/sql"
	"encoding/json"
	"errors"
	"fmt"
	"log/slog"
	"net/http"
	"strings"
	"sync"
	"time"
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

	flusher, ok := w.(http.Flusher)
	if ok == false {
		http.Error(w, "streaming not supported", http.StatusInternalServerError)
		return
	}

	w.Header().Set("Content-Type", "text/event-stream")
	w.Header().Set("Cache-Control", "no-cache")
	w.Header().Set("Connection", "keep-alive")

	subscription := s.collector.Subscribe(1)
	defer s.collector.Unsubscribe(subscription)
	requestContext := r.Context()

	writeError := func(err string) {
		fmt.Fprintf(w, "event: error\ndata: %s\n\n",
			strings.Replace(err, "\n", "\ndata: ", -1))
		flusher.Flush()
	}

	writeUpdate := func(d EnvironmentalDevice) {
		payload, err := json.Marshal(d)
		if err != nil {
			writeError(err.Error())
			return
		}
		fmt.Fprintf(w, "event: state\ndata: %s\n\n",
			strings.Replace(string(payload), "\n", "\ndata: ", -1))
		flusher.Flush()
	}

	ticker := time.NewTicker(time.Minute)
	defer ticker.Stop()

	for {
		select {
		case <-ticker.C:
			fmt.Fprintf(w, ": keepalive\n\n")
			flusher.Flush()
		case <-s.ctx.Done():
			logger.Info("done",
				slog.String("error", s.ctx.Err().Error()))
			writeError("EOS")
			return
		case <-requestContext.Done():
			logger.Info("disconnected",
				slog.String("error", requestContext.Err().Error()))
			return
		case d, ok := <-subscription:
			if ok == false {
				logger.Error("subscription early termination")
				writeError("internal error")
				return
			}
			writeUpdate(d)
			ticker.Reset(1 * time.Minute)
		}
	}
}

func extractLocationID(r *http.Request) (string, error) {
	locationID := r.PathValue("location_id")
	_, err := ParseLocationID(locationID)
	return locationID, err

}

func (s collectorHttpServer) handleEnvironmentalHistory(w http.ResponseWriter, r *http.Request) {
	logger := s.logger.With(
		slog.String("method", r.Method),
		slog.String("URI", r.RequestURI),
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

	readings, err := s.collector.journal.GetEnvironmentalHistory(ctx,
		locationID,
		time.Date(time.Now().Year(), 1, 1, 0, 0, 0, 0, time.Local), time.Now(),
	)

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

	if err := server.ListenAndServe(); err != nil && err != http.ErrServerClosed {
		s.logger.Error("server error",
			slog.String("error", err.Error()))
	}

	close(stopListen)
}
