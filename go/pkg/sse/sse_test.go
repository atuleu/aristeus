package sse

import (
	"context"
	"errors"
	"math"
	"net/http"
	"sync"
	"testing"
	"time"

	"github.com/stretchr/testify/mock"
	"github.com/stretchr/testify/suite"
)

type SSESuite struct {
	suite.Suite
	writer           *MockSSEWriter
	actual, expected []string
	flushCall        int
}

func (s *SSESuite) SetupTest() {
	s.writer = NewMockSSEWriter(s.T())
	s.flushCall = 0
	s.actual = nil
	s.expected = nil
	s.writer.EXPECT().Write(mock.Anything).RunAndReturn(func(bytes []byte) (int, error) {
		if s.Assert().Less(s.flushCall, len(s.actual), "unexpected write") == false {
			for len(s.actual) <= s.flushCall {
				s.actual = append(s.actual, "")
			}
		}
		s.actual[s.flushCall] += string(bytes)

		return len(bytes), nil
	}).Maybe()
}

func (s *SSESuite) TearDownTest() {
	s.Assert().Equal(s.expected, s.actual)
}

func (s *SSESuite) expectWrite(expected string) {
	s.expected = append(s.expected, expected)
	s.actual = append(s.actual, "")
	s.writer.EXPECT().Flush().Run(func() {
		s.flushCall += 1
		s.Require().LessOrEqual(s.flushCall, len(s.expected))
	}).Once()
}

func (s *SSESuite) TestDataSanization() {
	testdata := []struct {
		name, input, output string
	}{
		{"empty", "", ""},
		{"no_lines", "aaa bbb", "aaa bbb"},
		{"two_lines", "aaa\nbbb", "aaa\ndata: bbb"},
		{"multiple_lines", "aaa\rbbb\r\n   ccc\r\r", "aaa\ndata: bbb\ndata:    ccc\ndata: \ndata: "},
	}

	for _, d := range testdata {
		s.Run(d.name, func() {
			s.Assert().Equal(d.output, sanitizeData(d.input))
		})
	}
}

func (s *SSESuite) TestErrorWriting() {
	s.expectWrite("event: error\ndata: EOS\n\n")
	writeError(s.writer, EOS)
}

func (s *SSESuite) TestEventWriting() {
	type dummy struct {
		Field int `json:"field"`
	}
	testdata := []struct {
		name      string
		eventName string
		object    interface{}
		expected  string
	}{
		{"empty", "", struct{}{}, "data: {}\n\n"},
		{"no_name", "", dummy{Field: 42}, "data: {\"field\":42}\n\n"},
		{"with_name", "state_update", dummy{Field: 42}, "event: state_update\ndata: {\"field\":42}\n\n"},
		{"wrong_name+r", "state\rupdate", dummy{Field: 42}, "data: {\"field\":42}\n\n"},
		{"wrong_name_n", "state\nupdate", dummy{Field: 42}, "data: {\"field\":42}\n\n"},
		{"wrong_name_rn", "state\r\nupdate", dummy{Field: 42}, "data: {\"field\":42}\n\n"},
		{"intercept_errors", "state", errors.New("foobar"), "event: error\ndata: foobar\n\n"},
		{"intercept_serializaion_error", "", struct{ Field float64 }{math.NaN()}, "event: error\ndata: internal server error\n\n"},
	}
	for _, d := range testdata {
		s.Run(d.name, func() {
			s.expectWrite(d.expected)
			WriteEvent(s.writer, ServerSideEvent{Name: d.eventName, Data: d.object})
		})
	}

}

func (s *SSESuite) TestInvalidInterface() {
	w := NewMockResponseWriter(s.T())
	w.EXPECT().Header().Return(http.Header{})
	w.EXPECT().WriteHeader(http.StatusInternalServerError)
	w.EXPECT().Write(mock.Anything).RunAndReturn(func(value []byte) (int, error) {
		s.Assert().Equal(ErrNotSupported.Error()+"\n", string(value))
		return len(value), nil
	})
	s.Assert().ErrorIs(HandleSSE(w, nil, nil, 0), ErrNotSupported)
}

func (s *SSESuite) TestTerminateWithEOS() {
	req, err := http.NewRequestWithContext(context.Background(), "GET", "/", nil)
	s.Require().NoError(err)

	header := make(http.Header)
	s.writer.EXPECT().Header().Return(header)
	s.writer.EXPECT().WriteHeader(http.StatusOK)

	s.expectWrite("event: error\ndata: EOS\n\n")

	events := make(chan ServerSideEvent)
	var wg sync.WaitGroup

	wg.Go(func() {
		s.Assert().ErrorIs(HandleSSE(s.writer, req, events, 0), EOS)
	})
	defer wg.Wait()
	close(events)
}

func (s *SSESuite) TestTerminateWithCanceled() {
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	req, err := http.NewRequestWithContext(ctx, "GET", "/", nil)
	s.Require().NoError(err)

	header := make(http.Header)
	s.writer.EXPECT().Header().Return(header)
	s.writer.EXPECT().WriteHeader(http.StatusOK)

	// no write

	events := make(chan ServerSideEvent)
	defer close(events)
	var wg sync.WaitGroup

	wg.Go(func() {
		s.Assert().ErrorIs(HandleSSE(s.writer, req, events, 0), context.Canceled)
	})
	defer wg.Wait()
	cancel()
}

func (s *SSESuite) TestSendsEvents() {
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	req, err := http.NewRequestWithContext(ctx, "GET", "/", nil)
	s.Require().NoError(err)

	header := make(http.Header)
	s.writer.EXPECT().Header().Return(header)
	s.writer.EXPECT().WriteHeader(http.StatusOK)

	// no write

	events := make(chan ServerSideEvent)

	var wg sync.WaitGroup

	s.expectWrite("event: error\ndata: foobar\n\n")
	s.expectWrite("event: state\ndata: {\"Field\":42}\n\n")
	s.expectWrite("event: error\ndata: EOS\n\n")

	wg.Go(func() {
		s.Assert().ErrorIs(HandleSSE(s.writer, req, events, 0), EOS)
	})

	defer wg.Wait()
	events <- ServerSideEvent{Data: errors.New("foobar")}
	events <- ServerSideEvent{Data: struct{ Field int }{42}, Name: "state"}
	close(events)
}

func (s *SSESuite) TestKeepAlive() {
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	req, err := http.NewRequestWithContext(ctx, "GET", "/", nil)
	s.Require().NoError(err)

	header := make(http.Header)
	s.writer.EXPECT().Header().Return(header)
	s.writer.EXPECT().WriteHeader(http.StatusOK)

	// no write

	events := make(chan ServerSideEvent)

	var wg sync.WaitGroup

	s.expectWrite(":keepalive\n\n")
	s.expectWrite(":keepalive\n\n")
	s.expectWrite("event: state\ndata: {\"Field\":42}\n\n")
	s.expectWrite("event: state\ndata: {\"Field\":42}\n\n")
	s.expectWrite(":keepalive\n\n")
	s.expectWrite("event: error\ndata: EOS\n\n")

	wg.Go(func() {
		s.Assert().ErrorIs(HandleSSE(s.writer, req, events, 10*time.Millisecond), EOS)
	})
	defer wg.Wait()
	time.Sleep(25 * time.Millisecond)
	events <- ServerSideEvent{Data: struct{ Field int }{42}, Name: "state"}
	time.Sleep(5 * time.Millisecond)
	events <- ServerSideEvent{Data: struct{ Field int }{42}, Name: "state"}
	time.Sleep(15 * time.Millisecond)
	close(events)
}

func TestSSESuite(t *testing.T) {
	suite.Run(t, new(SSESuite))
}
