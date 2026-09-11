package collector

import (
	"cmp"
	"context"
	"database/sql"
	"errors"
	"fmt"
	"log/slog"
	"slices"
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
		return nil, errors.Join(err, res.db.Close())
	}
	slog.Info("using database",
		slog.String("path", path))
	return res, nil
}

func (s *SQLiteStore) Close() error {
	return s.db.Close()
}

func filterEnvironmentalReading(input []EnvironmentalReading) (locationsIDs []string, assignements []SensorAssignement, output []EnvironmentalReading, err error) {
	output = make([]EnvironmentalReading, 0, len(input))
	locationSet := make(map[string]bool)

	type untimedAssignments struct {
		SensorID, LocationID string
	}
	assignementsSet := make(map[untimedAssignments]bool)

	slices.SortStableFunc(input, func(a, b EnvironmentalReading) int {
		if n := cmp.Compare(a.LocationID, b.LocationID); n != 0 {
			return n
		}
		return a.Timestamp.Compare(b.Timestamp)
	})

	for _, r := range input {
		if r.Temperature_C == nil && r.Humidity_percent == nil && r.Pressure_hPa == nil && r.CO2_ppm == nil {
			continue
		}

		if len(r.LocationID) == 0 {
			return nil, nil, nil, errors.New("empty LocationID")
		}
		if len(r.SensorID) == 0 {
			return nil, nil, nil, errors.New("empty SensorID")
		}

		output = append(output, r)

		if locationSet[r.LocationID] == false {
			locationSet[r.LocationID] = true
			locationsIDs = append(locationsIDs, r.LocationID)
		}

		a := untimedAssignments{SensorID: r.SensorID, LocationID: r.LocationID}
		if assignementsSet[a] == false {
			assignementsSet[a] = true
			assignements = append(assignements, SensorAssignement{
				SensorID:    r.SensorID,
				LocationID:  r.LocationID,
				InstalledAt: r.Timestamp,
			})
		}
	}

	return
}

func (s *SQLiteStore) SaveEnvironmentalReadings(ctx context.Context, readings []EnvironmentalReading) (err error) {
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
	locationIDs, assignements, readings, err := filterEnvironmentalReading(readings)
	if err != nil {
		return fmt.Errorf("invalid EnvironmentalReading: %w", err)
	}

	if len(readings) == 0 {
		return errors.New("no non-empty readings")
	}

	tx, err := s.db.BeginTx(ctx, nil)
	if err != nil {
		return fmt.Errorf("failed to begin transaction: %w", err)
	}
	defer func() {
		if rbErr := tx.Rollback(); rbErr != nil && rbErr != sql.ErrTxDone {
			err = errors.Join(err, rbErr)
		}
	}()

	for _, locationID := range locationIDs {
		_, err := tx.ExecContext(ctx, locations_query, locationID)
		if err != nil {
			return fmt.Errorf("could not add missing '%s' location: %w", locationID, err)
		}
	}

	for _, assignement := range assignements {
		_, err := tx.ExecContext(ctx, clear_previous_assignments_query,
			sql.Named("sensor_id", assignement.SensorID),
			sql.Named("location_id", assignement.LocationID),
			sql.Named("timestamp", assignement.InstalledAt.Unix()),
		)
		if err != nil {
			return fmt.Errorf("could not removed outdated assignements: %w", err)
		}

		_, err = tx.ExecContext(ctx, add_missing_assginements_query,
			sql.Named("sensor_id", assignement.SensorID),
			sql.Named("location_id", assignement.LocationID),
			sql.Named("timestamp", assignement.InstalledAt.Unix()),
		)
		if err != nil {
			return fmt.Errorf("could not add missing assignements: %w", err)
		}

	}

	for _, r := range readings {
		_, err := tx.ExecContext(ctx, insertion_query,
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
	}

	err = tx.Commit()
	if err != nil {
		return fmt.Errorf("could not commit transaction: %w", err)
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
WHERE
	location_id = ?
	AND timestamp >= ?
	AND timestamp <= ?
ORDER BY timestamp ASC;
`
	rows, err := s.db.QueryContext(ctx, query, locationID, start.Unix(), end.Unix())
	if err != nil {
		return nil, fmt.Errorf("could not perform query: %w", err)
	}
	defer rows.Close()

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

	err = rows.Err()
	if err != nil {
		err = fmt.Errorf("incomplete read: %w", err)
	}

	return res, err
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
func (s *SQLiteStore) GetLocationAssignements(ctx context.Context, locationID string) ([]SensorAssignement, error) {
	query := `
SELECT
	sensor_id,
	location_id,
	installed_at,
	removed_at
FROM assignments
WHERE
	location_id = ?
ORDER BY installed_at ASC;
`
	rows, err := s.db.QueryContext(ctx, query, locationID)
	if err != nil {
		return nil, fmt.Errorf("could not perform assignment query: %w", err)
	}
	defer rows.Close()

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
	err = rows.Err()
	if err != nil {
		err = fmt.Errorf("incomplete read: %w", err)
	}

	return res, err

}

func newValue[T any](v T) *T {
	res := new(T)
	*res = v
	return res
}

func (s *SQLiteStore) GetSensorAssignements(ctx context.Context, sensorID string) ([]SensorAssignement, error) {

	query := `
SELECT
	sensor_id,
	location_id,
	installed_at,
	removed_at
FROM assignments
WHERE
	sensor_id = ?
ORDER BY installed_at ASC;
`
	rows, err := s.db.QueryContext(ctx, query, sensorID)
	if err != nil {
		return nil, fmt.Errorf("could not perform assignment query: %w", err)
	}
	defer rows.Close()

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

	err = rows.Err()
	if err != nil {
		err = fmt.Errorf("incomplete read: %w", err)
	}

	return res, err
}

func (s *SQLiteStore) GetActiveAssignments(ctx context.Context) ([]SensorAssignement, error) {
	query := `
SELECT
	sensor_id,
	location_id,
	installed_at
FROM assignments
WHERE
	removed_at IS NULL
ORDER BY location_id ASC;
`
	rows, err := s.db.QueryContext(ctx, query)
	if err != nil {
		return nil, fmt.Errorf("could not retrieve active assignments: %w", err)
	}
	defer rows.Close()

	var res []SensorAssignement = nil
	for rows.Next() {
		var a SensorAssignement
		var installedAt int64
		if err := rows.Scan(&a.SensorID, &a.LocationID, &installedAt); err != nil {
			return res, fmt.Errorf("could not parse assignment row: %w", err)
		}
		a.InstalledAt = time.Unix(installedAt, 0)
		res = append(res, a)
	}

	err = rows.Err()
	if err != nil {
		err = fmt.Errorf("incomplete read: %w", err)
	}

	return res, err

}

func (s *SQLiteStore) ensureSchema(ctx context.Context) (err error) {
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

	defer func() {
		if rbErr := tx.Rollback(); rbErr != nil && rbErr != sql.ErrTxDone {
			err = errors.Join(err, rbErr)
		}
	}()

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
