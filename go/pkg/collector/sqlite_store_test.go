package collector

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

func (s *SQLiteStoreTestSuite) TestIgnoresDuplicates() {
	t := time.Now()
	var err error
	err = s.journal.SaveEnvironmentalReadings(s.ctx, []EnvironmentalReading{
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
			Timestamp:        t.Round(time.Second),
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
	s.Assert().Len(values, 1)
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
		EnvironmentalReading{LocationID: "loc_c", SensorID: "sensor_e", Timestamp: b, Temperature_C: newValue(22.2)},
		EnvironmentalReading{LocationID: "loc_c", SensorID: "sensor_a", Timestamp: c, Temperature_C: newValue(22.2)},
	}))

	assignments, err := s.journal.GetSensorAssignements(s.ctx, "sensor_a")
	s.Require().NoError(err)
	s.Assert().Equal([]SensorAssignement{
		{LocationID: "loc_a", SensorID: "sensor_a", InstalledAt: a, RemovedAt: newValue(b)},
		{LocationID: "loc_c", SensorID: "sensor_a", InstalledAt: c},
	}, assignments)

	assignments, err = s.journal.GetSensorAssignements(s.ctx, "sensor_b")
	s.Require().NoError(err)
	s.Assert().Equal([]SensorAssignement{
		{LocationID: "loc_a", SensorID: "sensor_b", InstalledAt: b},
	}, assignments)

	assignments, err = s.journal.GetSensorAssignements(s.ctx, "sensor_d")
	s.Require().NoError(err)
	s.Assert().Equal([]SensorAssignement{
		{LocationID: "loc_b", SensorID: "sensor_d", InstalledAt: a},
	}, assignments)

	assignments, err = s.journal.GetSensorAssignements(s.ctx, "sensor_e")
	s.Require().NoError(err)
	s.Assert().Equal([]SensorAssignement{
		{LocationID: "loc_c", SensorID: "sensor_e", InstalledAt: b, RemovedAt: newValue(c)},
	}, assignments)

	assignments, err = s.journal.GetLocationAssignements(s.ctx, "loc_a")
	s.Require().NoError(err)
	s.Assert().Equal([]SensorAssignement{
		{LocationID: "loc_a", SensorID: "sensor_a", InstalledAt: a, RemovedAt: newValue(b)},
		{LocationID: "loc_a", SensorID: "sensor_b", InstalledAt: b, RemovedAt: nil},
	}, assignments)

	assignments, err = s.journal.GetLocationAssignements(s.ctx, "loc_b")
	s.Require().NoError(err)
	s.Assert().Equal([]SensorAssignement{
		{LocationID: "loc_b", SensorID: "sensor_d", InstalledAt: a},
	}, assignments)

	assignments, err = s.journal.GetLocationAssignements(s.ctx, "loc_c")

	s.Require().NoError(err)
	s.Assert().Equal([]SensorAssignement{
		{LocationID: "loc_c", SensorID: "sensor_e", InstalledAt: b, RemovedAt: newValue(c)},
		{LocationID: "loc_c", SensorID: "sensor_a", InstalledAt: c},
	}, assignments)

	assignments, err = s.journal.GetActiveAssignments(s.ctx)
	s.Require().NoError(err)
	s.Assert().Equal([]SensorAssignement{
		{LocationID: "loc_a", SensorID: "sensor_b", InstalledAt: b},
		{LocationID: "loc_b", SensorID: "sensor_d", InstalledAt: a},
		{LocationID: "loc_c", SensorID: "sensor_a", InstalledAt: c},
	}, assignments)

}

func TestSQLiteStoreTestSuite(t *testing.T) {
	suite.Run(t, new(SQLiteStoreTestSuite))
}
