package sse

import (
	"encoding/json"
	"errors"
	"fmt"
	"net/http"
	"strings"
	"time"
)

var (
	EOS             = errors.New("EOS")
	ErrInternal     = errors.New("internal server error")
	ErrNotSupported = errors.New("server-side events are not supported")
)

type ServerSideEvent struct {
	Name string
	Data interface{}
}

type SSEWriter interface {
	http.ResponseWriter
	http.Flusher
}

var newlineReplacer = strings.NewReplacer("\r\n", "\ndata: ", "\n", "\ndata: ", "\r", "\ndata: ")

func sanitizeData(payload string) string {
	return newlineReplacer.Replace(payload)
}

func writeError(w SSEWriter, err error) error {
	if _, err := fmt.Fprintf(w, "event: error\ndata: %s\n\n", sanitizeData(err.Error())); err != nil {
		return err
	}
	w.Flush()
	return nil
}

func WriteEvent(w SSEWriter, e ServerSideEvent) error {
	if err, ok := e.Data.(error); ok == true {
		return writeError(w, err)
	}

	payload, err := json.Marshal(e.Data)
	if err != nil {
		return writeError(w, ErrInternal)
	}

	if len(e.Name) != 0 && strings.ContainsAny(e.Name, "\n\r") == false {
		if _, err := fmt.Fprintf(w, "event: %s\n", e.Name); err != nil {
			return err
		}
	}
	if _, err := fmt.Fprintf(w, "data: %s\n\n", sanitizeData(string(payload))); err != nil {
		return err
	}
	w.Flush()
	return nil
}

func HandleSSE(w http.ResponseWriter, r *http.Request, events <-chan ServerSideEvent, keepAlive time.Duration) error {
	sseWriter, ok := w.(SSEWriter)
	if ok == false {
		http.Error(w, ErrNotSupported.Error(), http.StatusInternalServerError)
		return ErrNotSupported
	}
	requestCtx := r.Context()

	var ticker *time.Ticker
	if keepAlive > 0 {
		ticker = time.NewTicker(keepAlive)
		defer ticker.Stop()
	}

	w.Header().Set("Content-Type", "text/event-stream")
	w.Header().Set("Cache-Control", "no-cache")
	w.Header().Set("Connection", "keep-alive")
	w.WriteHeader(200)

	getTickerChan := func() <-chan time.Time {
		if ticker == nil {
			return nil
		}
		return ticker.C
	}

	for {
		select {
		case <-requestCtx.Done():
			return requestCtx.Err()
		case <-getTickerChan():
			if _, err := sseWriter.Write([]byte(":keepalive\n\n")); err != nil {
				return fmt.Errorf("write error: %w", err)
			}
			sseWriter.Flush()
		case e, ok := <-events:
			if ok == false {
				if err := writeError(sseWriter, EOS); err != nil {
					return fmt.Errorf("write error: %w", err)
				}
				return EOS
			}
			if err := WriteEvent(sseWriter, e); err != nil {
				return fmt.Errorf("write error: %w", err)
			}
			if ticker != nil {
				ticker.Reset(keepAlive)
			}
		}
	}
}
