package main

import (
	"context"
	"os"
	"path/filepath"
	"testing"
	"time"

	"github.com/stretchr/testify/suite"
)

type SQLiteStoreTestSuite struct {
	suite.Suite
	journal DataJournal
	tmpDir  string
	ctx     context.Context
	cancel  context.CancelFunc
}

func (s *SQLiteStoreTestSuite) SetupSuite() {
	s.tmpDir = s.T().TempDir()
}

func (s *SQLiteStoreTestSuite) SetupTest() {
	dbPath := filepath.Join(s.tmpDir, s.T().Name()+".db")
	var err error
	s.Require().NoError(os.MkdirAll(filepath.Dir(dbPath), 0755), "Failed to create tempdir")

	s.ctx, s.cancel = context.WithTimeout(context.Background(), 2*time.Second)

	s.journal, err = NewSQLiteStore(s.ctx, dbPath)
	s.Require().NoError(err, "Failed to create test database")
}

func (s *SQLiteStoreTestSuite) TearDownTest() {

	if s.journal != nil {
		s.Assert().NoError(s.journal.Close(), "Failed to close journal cleanly")
	}
	s.cancel()
}

func (s *SQLiteStoreTestSuite) TestSaveOutOfOrder() {
	t := time.Now()
	err := s.journal.SaveEnvironmentalReadings(s.ctx, []EnvironmentalReading{
		EnvironmentalReading{
			LocationID:       "foo",
			SensorID:         "02:02:02:02:02:02",
			Timestamp:        t.Round(time.Second),
			ReceivedAt:       t,
			Temperature_C:    newValue(22.2),
			Humidity_percent: newValue(44.4),
			Pressure_hPa:     newValue(1013.4),
			CO2_ppm:          newValue[uint](444),
		},
	})
	s.Assert().NoError(err)

	err = s.journal.SaveEnvironmentalReadings(s.ctx, []EnvironmentalReading{
		EnvironmentalReading{
			LocationID:       "foo",
			SensorID:         "02:02:02:02:02:02",
			Timestamp:        t.Round(time.Second).Add(-2 * time.Second),
			ReceivedAt:       t.Add(1 * time.Second),
			Temperature_C:    newValue(22.2),
			Humidity_percent: newValue(44.4),
			Pressure_hPa:     newValue(1013.4),
			CO2_ppm:          newValue[uint](444),
		},
		EnvironmentalReading{
			LocationID:       "foo",
			SensorID:         "02:02:02:02:02:02",
			Timestamp:        t.Round(time.Second).Add(-1 * time.Second),
			ReceivedAt:       t.Add(1 * time.Second),
			Temperature_C:    newValue(22.2),
			Humidity_percent: newValue(44.4),
			Pressure_hPa:     newValue(1013.4),
			CO2_ppm:          newValue[uint](444),
		},
	})
	s.Assert().NoError(err)

	values, err := s.journal.GetEnvironmentalHistory(s.ctx, "foo", time.Time{}, t.Add(10*time.Second))
	s.Assert().NoError(err)
	s.Assert().Len(values, 3)
	for i := 0; i < min(len(values), 3); i++ {
		s.Assert().Equal(t.Round(time.Second).Add(time.Duration(-2+i)*time.Second), values[i].Timestamp)
	}
}

func (s *SQLiteStoreTestSuite) TestAssignmentConsistency() {
	a := time.Now().Round(time.Second)
	b := a.Add(10 * time.Second)
	c := b.Add(10 * time.Second)
	s.Require().NoError(s.journal.SaveEnvironmentalReadings(s.ctx, []EnvironmentalReading{
		EnvironmentalReading{LocationID: "loc_a", SensorID: "sensor_a", Timestamp: a, Temperature_C: newValue(22.2)},
		EnvironmentalReading{LocationID: "loc_a", SensorID: "sensor_b", Timestamp: b, Temperature_C: newValue(22.2)},
		EnvironmentalReading{LocationID: "loc_a", SensorID: "sensor_b", Timestamp: c, Temperature_C: newValue(22.2)},
		EnvironmentalReading{LocationID: "loc_b", SensorID: "sensor_d", Timestamp: a, Temperature_C: newValue(22.2)},
		EnvironmentalReading{LocationID: "loc_b", SensorID: "sensor_d", Timestamp: b, Temperature_C: newValue(22.2)},
		EnvironmentalReading{LocationID: "loc_b", SensorID: "sensor_d", Timestamp: c, Temperature_C: newValue(22.2)},
		EnvironmentalReading{LocationID: "loc_c", SensorID: "sensor_e", Timestamp: a, Temperature_C: newValue(22.2)},
		EnvironmentalReading{LocationID: "loc_c", SensorID: "sensor_a", Timestamp: c, Temperature_C: newValue(22.2)},
	}))

	assignments, err := s.journal.GetSensorAssignement(s.ctx, "sensor_a")
	s.Require().NoError(err)
	if s.Assert().Len(assignments, 2) == true {
		s.Assert().Equal("sensor_a", assignments[0].SensorID)
		s.Assert().Equal("loc_a", assignments[0].LocationID)
		s.Assert().Equal(a, assignments[0].InstalledAt, "a")
		if s.Assert().NotNil(assignments[0].RemovedAt) == true {
			s.Assert().Equal(b, *assignments[0].RemovedAt, "b")
		}

		s.Assert().Equal("sensor_a", assignments[1].SensorID)
		s.Assert().Equal("loc_c", assignments[1].LocationID)
		s.Assert().Equal(c, assignments[1].InstalledAt)
		s.Assert().Nil(assignments[1].RemovedAt)

	}

	assignments, err = s.journal.GetSensorAssignement(s.ctx, "sensor_b")
	s.Require().NoError(err)
	if s.Assert().Len(assignments, 1) == true {
		s.Assert().Equal("sensor_b", assignments[0].SensorID)
		s.Assert().Equal("loc_a", assignments[0].LocationID)
		s.Assert().Equal(b, assignments[0].InstalledAt, "b")
		s.Assert().Nil(assignments[0].RemovedAt)
	}

	assignments, err = s.journal.GetSensorAssignement(s.ctx, "sensor_d")
	s.Require().NoError(err)
	if s.Assert().Len(assignments, 1) == true {
		s.Assert().Equal("sensor_d", assignments[0].SensorID)
		s.Assert().Equal("loc_b", assignments[0].LocationID)
		s.Assert().Equal(a, assignments[0].InstalledAt, "a")
		s.Assert().Nil(assignments[0].RemovedAt)
	}

	assignments, err = s.journal.GetSensorAssignement(s.ctx, "sensor_e")
	s.Require().NoError(err)
	if s.Assert().Len(assignments, 1) == true {
		s.Assert().Equal("sensor_e", assignments[0].SensorID)
		s.Assert().Equal("loc_c", assignments[0].LocationID)
		s.Assert().Equal(a, assignments[0].InstalledAt, "a")
		if s.Assert().NotNil(assignments[0].RemovedAt) == true {
			s.Assert().Equal(c, *assignments[0].RemovedAt, "c")
		}
	}

	assignments, err = s.journal.GetLocationAssignement(s.ctx, "loc_a")
	s.Require().NoError(err)
	if s.Assert().Len(assignments, 2) == true {
		s.Assert().Equal("sensor_a", assignments[0].SensorID)
		s.Assert().Equal("loc_a", assignments[0].LocationID)
		s.Assert().Equal(a, assignments[0].InstalledAt, "a")
		if s.Assert().NotNil(assignments[0].RemovedAt) == true {
			s.Assert().Equal(b, *assignments[0].RemovedAt, "b")
		}

		s.Assert().Equal("sensor_b", assignments[1].SensorID)
		s.Assert().Equal("loc_a", assignments[1].LocationID)
		s.Assert().Equal(b, assignments[1].InstalledAt, "b")
		s.Assert().Nil(assignments[1].RemovedAt)

	}

	assignments, err = s.journal.GetLocationAssignement(s.ctx, "loc_b")
	s.Require().NoError(err)
	if s.Assert().Len(assignments, 1) == true {
		s.Assert().Equal("sensor_d", assignments[0].SensorID)
		s.Assert().Equal("loc_b", assignments[0].LocationID)
		s.Assert().Equal(a, assignments[0].InstalledAt, "a")
		s.Assert().Nil(assignments[0].RemovedAt)
	}

	assignments, err = s.journal.GetLocationAssignement(s.ctx, "loc_c")
	s.Require().NoError(err)
	if s.Assert().Len(assignments, 2) == true {
		s.Assert().Equal("sensor_e", assignments[0].SensorID)
		s.Assert().Equal("loc_c", assignments[0].LocationID)
		s.Assert().Equal(a, assignments[0].InstalledAt, "a")
		if s.Assert().NotNil(assignments[0].RemovedAt) == true {
			s.Assert().Equal(c, *assignments[0].RemovedAt, "c")
		}

		s.Assert().Equal("sensor_a", assignments[1].SensorID)
		s.Assert().Equal("loc_c", assignments[1].LocationID)
		s.Assert().Equal(c, assignments[1].InstalledAt, "c")
		s.Assert().Nil(assignments[1].RemovedAt)
	}

}

func TestSQLiteStoreTestSuite(t *testing.T) {
	suite.Run(t, new(SQLiteStoreTestSuite))
}
