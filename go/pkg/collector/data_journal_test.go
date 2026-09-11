package collector

import (
	"context"
	"time"

	"github.com/stretchr/testify/suite"
)

type DataJournalSuite struct {
	suite.Suite
	journal    DataJournal
	SetupStore func(context.Context) (DataJournal, error)
	ctx        context.Context
	cancel     context.CancelFunc
}

func (s *DataJournalSuite) SetupTest() {
	s.ctx, s.cancel = context.WithTimeout(context.Background(), 2*time.Second)
	var err error
	s.journal, err = s.SetupStore(s.ctx)
	s.Require().NoError(err, "failed to create data journal")
}

func (s *DataJournalSuite) TearDownTest() {
	if s.journal != nil {
		s.Assert().NoError(s.journal.Close(), "Failed to close journal cleanly")
	}
	s.cancel()
}

func (s *DataJournalSuite) TestSaveEnvironmentOutOfOrder() {
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

func (s *DataJournalSuite) TestIgnoresDuplicates() {
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

func (s *DataJournalSuite) TestAssignmentConsistency() {
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

func (s *DataJournalSuite) TestScaleReadingsIO() {
	t := time.Date(2026, 1, 1, 0, 0, 0, 0, time.Local)
	var err error
	err = s.journal.SaveScaleReading(s.ctx, []ScaleReading{
		ScaleReading{
			LocationID: "hive_001_general",
			SensorID:   "02:02:02:02:02:02",
			Timestamp:  t.Add(1 * time.Minute),
			ReceivedAt: t.Add(1*time.Minute + 8*time.Millisecond),
			Total_kg:   newValue(45.3),
		},
	})
	s.Require().NoError(err)

	expected := []ScaleReading{
		ScaleReading{
			LocationID:        "hive_001_general",
			SensorID:          "02:02:02:02:02:02",
			Timestamp:         t,
			ReceivedAt:        t.Add(12 * time.Millisecond),
			Total_kg:          newValue(45.2),
			Temperature_C:     newValue(23.0),
			Humidity_percent:  newValue(43.0),
			CellFrontLeft_kg:  newValue(11.3),
			CellFrontRight_kg: newValue(11.3),
			CellBackLeft_kg:   newValue(11.2),
			CellBackRight_kg:  newValue(11.4),
		},
		ScaleReading{
			LocationID:        "hive_001_general",
			SensorID:          "02:02:02:02:02:02",
			Timestamp:         t.Add(1 * time.Minute),
			ReceivedAt:        t.Add(1*time.Minute + 10*time.Millisecond),
			Total_kg:          newValue(45.3),
			Temperature_C:     newValue(23.0),
			Humidity_percent:  newValue(43.0),
			CellFrontLeft_kg:  newValue(11.4),
			CellFrontRight_kg: newValue(11.3),
			CellBackLeft_kg:   newValue(11.2),
			CellBackRight_kg:  newValue(11.4),
		},
	}

	err = s.journal.SaveScaleReading(s.ctx, expected)
	expected[1].ReceivedAt = t.Add(1*time.Minute + 8*time.Millisecond)
	s.Require().NoError(err)

	readings, err := s.journal.GetScaleHistory(s.ctx, "hive_001_general", t, t.Add(1*time.Minute))
	s.Require().NoError(err)
	s.Assert().Equal(expected, readings)

	assignements, err := s.journal.GetActiveAssignments(s.ctx)
	s.Require().NoError(err)
	s.Assert().Equal([]SensorAssignement{
		{SensorID: "02:02:02:02:02:02", LocationID: "hive_001_general", InstalledAt: t.Add(1 * time.Minute)},
	}, assignements)

}

func (s *DataJournalSuite) TestTrafficIO() {
	t := time.Date(2026, 1, 1, 0, 0, 0, 0, time.Local)
	var err error
	err = s.journal.SaveTrafficCount(s.ctx, []TrafficCount{
		TrafficCount{
			LocationID: "hive_001_general",
			Timestamp:  t.Add(1 * time.Minute),
			Duration:   time.Minute,
			ReceivedAt: t.Add(1*time.Minute + 8*time.Millisecond),
			Outgoing:   10,
			Ingoing:    3,
		},
	})
	s.Require().NoError(err)

	expected := []TrafficCount{
		TrafficCount{
			LocationID: "hive_001_general",
			Timestamp:  t,
			Duration:   time.Minute,
			ReceivedAt: t.Add(10 * time.Millisecond),
			Outgoing:   8,
			Ingoing:    9,
		},
		TrafficCount{
			LocationID: "hive_001_general",
			Timestamp:  t.Add(1 * time.Minute),
			Duration:   time.Minute,
			ReceivedAt: t.Add(1*time.Minute + 10*time.Millisecond),
			Outgoing:   10,
			Ingoing:    5,
		},
	}

	err = s.journal.SaveTrafficCount(s.ctx, expected)
	expected[1].ReceivedAt = t.Add(1*time.Minute + 8*time.Millisecond)
	s.Require().NoError(err)

	readings, err := s.journal.GetTrafficHistory(s.ctx, "hive_001_general", t, t.Add(1*time.Minute))
	s.Require().NoError(err)
	s.Assert().Equal(expected, readings)
}
