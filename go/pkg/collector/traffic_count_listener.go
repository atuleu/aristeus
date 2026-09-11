package collector

import (
	"context"
	"encoding/json"
	"log/slog"
	"net"
	"time"

	"github.com/atuleu/aristeus/go/pkg/arisble"
)

type trafficCount struct {
	Timestamp   time.Time `json:"timestamp"`
	HiveID      uint      `json:"hive_id"`
	Duration_ms int64     `json:"duration_ms"`
	Outgoing    int64     `json:"outgoing"`
	Ingoing     int64     `json:"ingoing"`
}

func ListenTrafficCount(ctx context.Context, ch chan<- TrafficCount, addr string) error {
	defer close(ch)

	udpAddr, err := net.ResolveUDPAddr("udp", addr)
	if err != nil {
		return err
	}

	conn, err := net.ListenUDP("udp", udpAddr)
	if err != nil {
		return err
	}
	defer conn.Close()

	go func() {
		<-ctx.Done()
		conn.Close()
	}()

	logger := slog.With("module", "UDPTrafficCountListener")

	logger.Info("listening")
	buf := make([]byte, 4096)
	for {
		n, _, err := conn.ReadFromUDP(buf)
		if err != nil {
			select {
			case <-ctx.Done():
				return ctx.Err()
			default:
				return err
			}
		}

		var c trafficCount
		err = json.Unmarshal(buf[0:n], &c)
		if err != nil {
			logger.Error("could not unmarshal packet",
				slog.String("error", err.Error()))
			continue

		}
		newCount := TrafficCount{
			LocationID: BuildLocationID(arisble.Location{HiveID: uint8(c.HiveID),
				Placement: arisble.PlacementGeneral,
			}),
			Timestamp:  c.Timestamp,
			ReceivedAt: time.Now(),
			Duration:   time.Duration(c.Duration_ms) * time.Millisecond,
			Ingoing:    int(c.Ingoing),
			Outgoing:   int(c.Outgoing),
		}
		select {
		case ch <- newCount:
		default:
			logger.Error("dropping count due to overflow",
				slog.String("location_id", newCount.LocationID),
				slog.Time("timestamp", newCount.Timestamp),
			)
		}
	}

}
