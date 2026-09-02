package main

import (
	"context"
	"database/sql"
	"fmt"
	"time"

	_ "modernc.org/sqlite"
)

type SQLiteStore struct {
	db *sql.DB
}

func NewSQLiteStore(ctx context.Context, path string) (DataJournal, error) {
	db, err := sql.Open("sqlite", path)
	if err != nil {
		return nil, fmt.Errorf("could not open SQLite db '%s': %w", path, err)
	}

	res := &SQLiteStore{db: db}
	res.db.SetMaxOpenConns(1)
	res.db.SetMaxIdleConns(1)

	err = res.ensureSchema(ctx)
	if err != nil {
		return nil, err
	}

	return &SQLiteStore{db: db}, nil
}

func (s *SQLiteStore) Close() error {
	return s.db.Close()
}

func (s *SQLiteStore) SaveEnvironmentalReadings(ctx context.Context, readings []EnvironmentalReading) error {
	insertion_query := `INSERT OR IGNORE INTO environmental_readings (
location_id,
sensor_id,
timestamp,
received_at,
temperature_c,
humidity_percent,
pressure_hpa,
co2_ppm
) VALUES (?,?,?,?,?,?,?,?);
`

	for _, r := range readings {
		if len(r.LocationID) == 0 {
			return fmt.Errorf("LocationID cannot be empty")
		}
		if len(r.SensorID) == 0 {
			return fmt.Errorf("SensorID cannot be empty")
		}

	}

	for _, r := range readings {
		if err := s.maintainLocationAndAssignments(ctx, r.LocationID, r.SensorID, r.Timestamp); err != nil {
			return err
		}
	}

	added := 0
	for _, r := range readings {
		if r.Temperature_C == nil && r.Humidity_percent == nil && r.Pressure_hPa == nil && r.CO2_ppm == nil {
			continue
		}

		_, err := s.db.ExecContext(ctx, insertion_query,
			r.LocationID,
			r.SensorID,
			r.Timestamp.Unix(),
			r.ReceivedAt.UnixMilli(),
			r.Temperature_C,
			r.Humidity_percent,
			r.Pressure_hPa,
			r.CO2_ppm)
		if err != nil {
			return fmt.Errorf("could not store reading (%s,%s): %w", r.LocationID, r.Timestamp, err)
		}
		added += 1
	}

	if added == 0 && len(readings) != 0 {
		return fmt.Errorf("all records were empty")
	}
	return nil
}

func (s *SQLiteStore) maintainLocationAndAssignments(ctx context.Context, locationID, sensorID string, timestamp time.Time) error {
	if len(locationID) == 0 {
		return fmt.Errorf("LocationID cannot be empty")
	}
	if len(sensorID) == 0 {
		return fmt.Errorf("SensorID cannot be empty")
	}

	locations_query := `
INSERT OR IGNORE INTO locations (location_id)
VALUES (?);
`

	clear_previous_assignments_query := `
	UPDATE assignments
	SET removed_at = :timestamp
	WHERE removed_at IS NULL
		AND (
			(sensor_id = :sensor_id AND location_id != :location_id)
			OR
			(sensor_id != :sensor_id AND location_id = :location_id)
			);
`

	add_missing_assginements_query := `
	INSERT INTO assignments (sensor_id,location_id,installed_at,removed_at)
SELECT :sensor_id,:location_id,:timestamp, NULL
WHERE NOT EXISTS (
	SELECT 1 FROM assignments
	WHERE sensor_id = :sensor_id
		AND location_id = :location_id
		AND removed_at IS NULL
);
`

	_, err := s.db.ExecContext(ctx, locations_query, locationID)
	if err != nil {
		return fmt.Errorf("could not add missing location: %w", err)
	}

	tx, err := s.db.BeginTx(ctx, nil)
	if err != nil {
		return fmt.Errorf("failed to begin transaction: %w", err)
	}

	_, err = tx.ExecContext(ctx, clear_previous_assignments_query,
		sql.Named("sensor_id", sensorID),
		sql.Named("location_id", locationID),
		sql.Named("timestamp", timestamp.Unix()))
	if err != nil {
		return fmt.Errorf("could not remove outdated assignments: %w", err)
	}

	_, err = tx.ExecContext(ctx, add_missing_assginements_query,
		sql.Named("sensor_id", sensorID),
		sql.Named("location_id", locationID),
		sql.Named("timestamp", timestamp.Unix()))
	if err != nil {
		return fmt.Errorf("could not add missing assignments: %w", err)
	}

	err = tx.Commit()
	if err != nil {
		return fmt.Errorf("could not update assignments: %w", err)
	}
	return nil
}

func (s *SQLiteStore) GetEnvironmentalHistory(ctx context.Context, locationID string, start, end time.Time) ([]EnvironmentalReading, error) {
	query := `
SELECT
	sensor_id,
	timestamp,
	received_at,
	temperature_c,
	humidity_percent,
	pressure_hpa,
	co2_ppm
FROM environmental_readings
WHERE location_id = ?
  AND timestamp >= ?
  AND timestamp <= ?;
`
	rows, err := s.db.QueryContext(ctx, query, locationID, start.Unix(), end.Unix())
	if err != nil {
		return nil, fmt.Errorf("could not perform query: %w", err)
	}
	res := make([]EnvironmentalReading, 0)
	for rows.Next() {
		r := EnvironmentalReading{LocationID: locationID}
		var timestamp, receivedAt int64
		if err := rows.Scan(&r.SensorID, &timestamp, &receivedAt, &r.Temperature_C, &r.Humidity_percent, &r.Pressure_hPa, &r.CO2_ppm); err != nil {
			return res, fmt.Errorf("could not scan result row: %w", err)
		}
		r.Timestamp = time.Unix(timestamp, 0)
		r.ReceivedAt = time.UnixMilli(receivedAt)
		res = append(res, r)
	}

	return res, nil
}

func (s *SQLiteStore) SaveLocation(ctx context.Context, loc SensorLocation) error {
	query := `
INSERT INTO locations (location_id, hive_id, description)
VALUES (?, ?, ?)
ON CONFLICT(location_id) DO UPDATE SET
  hive_id = excluded.hive_id,
  description = excluded.description;
`
	_, err := s.db.ExecContext(ctx, query, loc.LocationID, loc.HiveID, loc.Description)
	if err != nil {
		return fmt.Errorf("failed to update location_id=%s: %w", loc.LocationID, err)
	}

	return nil
}

func (s *SQLiteStore) GetSensorLocations(ctx context.Context) ([]SensorLocation, error) {
	return nil, fmt.Errorf("not yet implemented")
}
func (s *SQLiteStore) GetLocationAssignement(ctx context.Context, locationID string) ([]SensorAssignement, error) {
	query := `
SELECT
	sensor_id,
	location_id,
	installed_at,
	removed_at
FROM assignments
WHERE
	location_id = ?;
`
	rows, err := s.db.QueryContext(ctx, query, locationID)
	if err != nil {
		return nil, fmt.Errorf("could not perform assignment query: %w", err)
	}
	var res []SensorAssignement
	for rows.Next() {
		var a SensorAssignement
		var (
			installedAt int64
			removedAt   *int64
		)

		if err := rows.Scan(&a.SensorID, &a.LocationID, &installedAt, &removedAt); err != nil {
			return res, fmt.Errorf("could not scan assignment row: %w", err)
		}
		a.InstalledAt = time.Unix(installedAt, 0)
		if removedAt != nil {
			a.RemovedAt = newValue(time.Unix(*removedAt, 0))
		}
		res = append(res, a)
	}
	return res, nil

}

func newValue[T any](v T) *T {
	res := new(T)
	*res = v
	return res
}

func (s *SQLiteStore) GetSensorAssignement(ctx context.Context, sensorID string) ([]SensorAssignement, error) {

	query := `
SELECT
	sensor_id,
	location_id,
	installed_at,
	removed_at
FROM assignments
WHERE
	sensor_id = ?;
`
	rows, err := s.db.QueryContext(ctx, query, sensorID)
	if err != nil {
		return nil, fmt.Errorf("could not perform assignment query: %w", err)
	}
	var res []SensorAssignement
	for rows.Next() {
		var a SensorAssignement
		var (
			installedAt int64
			removedAt   *int64
		)
		if err := rows.Scan(&a.SensorID, &a.LocationID, &installedAt, &removedAt); err != nil {
			return res, fmt.Errorf("could not scan assignment row: %w", err)
		}
		a.InstalledAt = time.Unix(installedAt, 0)
		if removedAt != nil {
			a.RemovedAt = newValue[time.Time](time.Unix(*removedAt, 0))
		}

		res = append(res, a)
	}
	return res, nil
}

func (s *SQLiteStore) ensureSchema(ctx context.Context) error {
	pragmas := `
PRAGMA journal_mode=WAL;
PRAGMA synchronous= NORMAL;
PRAGMA foreign_keys=ON;
PRAGMA busy_timeout=5000;
PRAGMA temp_store= MEMORY;
`
	if _, err := s.db.ExecContext(ctx, pragmas); err != nil {
		return fmt.Errorf("failed to set pragmas: %w", err)
	}
	tx, err := s.db.BeginTx(ctx, nil)
	if err != nil {
		return fmt.Errorf("failed to begin migration tx: %w", err)
	}

	schema := `
-- 1. Locations
CREATE TABLE IF NOT EXISTS locations (
location_id TEXT PRIMARY KEY,
hive_id TEXT,
description TEXT
);

-- 2. Assignements
CREATE TABLE IF NOT EXISTS assignments (
sensor_id TEST NOT NULL,
location_id TEXT NOT NULL,
installed_at INTEGER NOT NULL,
removed_at INTEGER,
PRIMARY KEY (sensor_id,installed_at)
FOREIGN KEY (location_id) REFERENCES locations(location_id)
);

-- 3. Index for active assignments
CREATE INDEX IF NOT EXISTS idx_active_assignments
ON assignments(sensor_id) WHERE removed_at IS NULL;

-- 4. Environmental readings
CREATE TABLE IF NOT EXISTS environmental_readings (
location_id TEXT NOT NULL,
sensor_id TEXT NOT NULL,
timestamp INTEGER NOT NULL,
received_at INTEGER NOT NULL,
temperature_c REAL,
humidity_percent REAL,
pressure_hpa REAL,
co2_ppm INTEGER,
PRIMARY KEY (location_id,timestamp),
FOREIGN KEY (location_id) REFERENCES locations(location_id)
) WITHOUT ROWID;

-- 4. Scake readings
CREATE TABLE IF NOT EXISTS scale_readings (
location_id TEXT NOT NULL,
sensor_id TEXT NOT NULL,
timestamp INTEGER NOT NULL,
received_at INTEGER NOT NULL,
temperature_c REAL,
humidity_percent REAL,
total_weight_kg REAL,
cell_0_kg REAL,
cell_1_kg REAL,
cell_2_kg REAL,
cell_3_kg REAL,
PRIMARY KEY (location_id,timestamp),
FOREIGN KEY (location_id) REFERENCES locations(location_id)
) WITHOUT ROWID;

`
	if _, err := tx.ExecContext(ctx, schema); err != nil {
		return fmt.Errorf("failed to execute schema: %w", err)
	}

	if err := tx.Commit(); err != nil {
		return fmt.Errorf("failed to commit schema: %w", err)
	}

	return nil
}
