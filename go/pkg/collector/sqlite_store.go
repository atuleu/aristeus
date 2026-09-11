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

type reading interface {
	getTimestamp() time.Time
	getLocationID() string
	getSensorID() string
	isEmpty() bool
}

func (r EnvironmentalReading) getLocationID() string   { return r.LocationID }
func (r EnvironmentalReading) getSensorID() string     { return r.SensorID }
func (r EnvironmentalReading) getTimestamp() time.Time { return r.Timestamp }
func (r EnvironmentalReading) isEmpty() bool {
	return r.Temperature_C == nil && r.Humidity_percent == nil && r.Pressure_hPa == nil && r.CO2_ppm == nil
}

func filterReadings[T reading](input []T) (locationsIDs []string, assignements []SensorAssignement, output []T, err error) {
	output = make([]T, 0, len(input))
	locationSet := make(map[string]bool)

	type untimedAssignments struct {
		SensorID, LocationID string
	}
	assignementsSet := make(map[untimedAssignments]bool)

	slices.SortStableFunc(input, func(a, b T) int {
		if n := cmp.Compare(a.getLocationID(), b.getLocationID()); n != 0 {
			return n
		}
		return a.getTimestamp().Compare(b.getTimestamp())
	})

	for _, r := range input {
		if r.isEmpty() {
			continue
		}

		if len(r.getLocationID()) == 0 {
			return nil, nil, nil, errors.New("empty LocationID")
		}
		if len(r.getSensorID()) == 0 {
			return nil, nil, nil, errors.New("empty SensorID")
		}

		output = append(output, r)

		if locationSet[r.getLocationID()] == false {
			locationSet[r.getLocationID()] = true
			locationsIDs = append(locationsIDs, r.getLocationID())
		}

		a := untimedAssignments{SensorID: r.getSensorID(), LocationID: r.getLocationID()}
		if assignementsSet[a] == false {
			assignementsSet[a] = true
			assignements = append(assignements, SensorAssignement{
				SensorID:    r.getSensorID(),
				LocationID:  r.getLocationID(),
				InstalledAt: r.getTimestamp(),
			})
		}
	}

	return
}

func (s *SQLiteStore) includeTopologyUpdateToTx(ctx context.Context, tx *sql.Tx, locationIDs []string, assignements []SensorAssignement) error {
	var clear_previous_assignments_query = `
	UPDATE assignments
	SET removed_at = :timestamp
	WHERE removed_at IS NULL
		AND (
			(sensor_id = :sensor_id AND location_id != :location_id)
			OR
			(sensor_id != :sensor_id AND location_id = :location_id)
			);
`
	var add_missing_assginements_query = `
	INSERT INTO assignments (sensor_id,location_id,installed_at,removed_at)
SELECT :sensor_id,:location_id,:timestamp, NULL
WHERE NOT EXISTS (
	SELECT 1 FROM assignments
	WHERE sensor_id = :sensor_id
		AND location_id = :location_id
		AND removed_at IS NULL
);
`
	var locations_query = `
INSERT OR IGNORE INTO locations (location_id)
VALUES (?);
`
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
			sql.Named("timestamp", assignement.InstalledAt.UnixMilli()),
		)
		if err != nil {
			return fmt.Errorf("could not removed outdated assignements: %w", err)
		}

		_, err = tx.ExecContext(ctx, add_missing_assginements_query,
			sql.Named("sensor_id", assignement.SensorID),
			sql.Named("location_id", assignement.LocationID),
			sql.Named("timestamp", assignement.InstalledAt.UnixMilli()),
		)
		if err != nil {
			return fmt.Errorf("could not add missing assignements: %w", err)
		}
	}
	return nil
}

func (s *SQLiteStore) SaveEnvironmentalReadings(ctx context.Context, readings []EnvironmentalReading) (err error) {
	insertion_query := `
INSERT INTO environmental_readings (
	location_id,
	sensor_id,
	timestamp,
	received_at,
	temperature_c,
	humidity_percent,
	pressure_hpa,
	co2_ppm
) VALUES (
	:location_id,
	:sensor_id,
	:timestamp,
	:received_at,
	:temperature_c,
	:humidity_percent,
	:pressure_hpa,
	:co2_ppm
)
ON CONFLICT (location_id,timestamp)
DO UPDATE SET
	temperature_c = COALESCE(EXCLUDED.temperature_c,environmental_readings.temperature_c),
	humidity_percent = COALESCE(EXCLUDED.humidity_percent,environmental_readings.humidity_percent),
	pressure_hpa = COALESCE(EXCLUDED.pressure_hpa,environmental_readings.pressure_hpa),
	co2_ppm = COALESCE(EXCLUDED.co2_ppm,environmental_readings.co2_ppm);
`

	locationIDs, assignements, readings, err := filterReadings(readings)
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

	err = s.includeTopologyUpdateToTx(ctx, tx, locationIDs, assignements)
	if err != nil {
		return err
	}

	for _, r := range readings {
		_, err := tx.ExecContext(ctx, insertion_query,
			sql.Named("location_id", r.LocationID),
			sql.Named("sensor_id", r.SensorID),
			sql.Named("timestamp", r.Timestamp.UnixMilli()),
			sql.Named("received_at", r.ReceivedAt.UnixMilli()),
			sql.Named("temperature_c", r.Temperature_C),
			sql.Named("humidity_percent", r.Humidity_percent),
			sql.Named("pressure_hpa", r.Pressure_hPa),
			sql.Named("co2_ppm", r.CO2_ppm),
		)

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

	rows, err := s.db.QueryContext(ctx, query, locationID, start.UnixMilli(), end.UnixMilli())
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
		r.Timestamp = time.UnixMilli(timestamp)
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
		a.InstalledAt = time.UnixMilli(installedAt)
		if removedAt != nil {
			a.RemovedAt = newValue(time.UnixMilli(*removedAt))
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
		a.InstalledAt = time.UnixMilli(installedAt)
		if removedAt != nil {
			a.RemovedAt = newValue[time.Time](time.UnixMilli(*removedAt))
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
		a.InstalledAt = time.UnixMilli(installedAt)
		res = append(res, a)
	}

	err = rows.Err()
	if err != nil {
		err = fmt.Errorf("incomplete read: %w", err)
	}

	return res, err

}

func (r ScaleReading) getLocationID() string   { return r.LocationID }
func (r ScaleReading) getSensorID() string     { return r.SensorID }
func (r ScaleReading) getTimestamp() time.Time { return r.Timestamp }
func (r ScaleReading) isEmpty() bool {
	return r.Temperature_C == nil &&
		r.Humidity_percent == nil &&
		r.Total_kg == nil &&
		r.CellFrontLeft_kg == nil &&
		r.CellFrontRight_kg == nil &&
		r.CellBackLeft_kg == nil &&
		r.CellBackRight_kg == nil
}

func (s *SQLiteStore) SaveScaleReading(ctx context.Context, readings []ScaleReading) (err error) {
	query := `
INSERT OR IGNORE INTO scale_readings (
	location_id,
	sensor_id,
	timestamp,
	received_at,
	temperature_c,
	humidity_percent,
	total_weight_kg,
	cell_front_left_kg,
	cell_front_right_kg,
	cell_back_left_kg,
	cell_back_right_kg
) VALUES (
	:location_id,
	:sensor_id,
	:timestamp,
	:received_at,
	:temperature_c,
	:humidity_percent,
	:total_kg,
	:cell_front_left_kg,
	:cell_front_right_kg,
	:cell_back_left_kg,
	:cell_back_right_kg
)
ON CONFLICT (location_id,timestamp)
DO UPDATE SET
	temperature_c = COALESCE(EXCLUDED.temperature_c,scale_readings.temperature_c),
	humidity_percent = COALESCE(EXCLUDED.humidity_percent,scale_readings.humidity_percent),
	total_weight_kg = COALESCE(EXCLUDED.total_weight_kg,scale_readings.total_weight_kg),
	cell_front_left_kg = COALESCE(EXCLUDED.cell_front_left_kg,scale_readings.cell_front_left_kg),
	cell_front_right_kg = COALESCE(EXCLUDED.cell_front_right_kg,scale_readings.cell_front_right_kg),
	cell_back_left_kg = COALESCE(EXCLUDED.cell_back_left_kg,scale_readings.cell_back_left_kg),
	cell_back_right_kg = COALESCE(EXCLUDED.cell_back_right_kg,scale_readings.cell_back_right_kg);
`
	locationIDs, assignements, readings, err := filterReadings(readings)
	if err != nil {
		return fmt.Errorf("invalid ScaleReadings: %w", err)
	}
	if len(readings) == 0 {
		return errors.New("no non-empty readings")
	}
	tx, err := s.db.BeginTx(ctx, nil)
	if err != nil {
		return fmt.Errorf("failed to begin transaction: %w", err)
	}
	defer func() {
		if rbErr := tx.Rollback(); rbErr != sql.ErrTxDone {
			err = errors.Join(err, rbErr)
		}
	}()

	err = s.includeTopologyUpdateToTx(ctx, tx, locationIDs, assignements)
	if err != nil {
		return err
	}

	for _, r := range readings {
		_, err := tx.ExecContext(ctx, query,
			sql.Named("location_id", r.LocationID),
			sql.Named("sensor_id", r.SensorID),
			sql.Named("timestamp", r.Timestamp.UnixMilli()),
			sql.Named("received_at", r.ReceivedAt.UnixMilli()),
			sql.Named("temperature_c", r.Temperature_C),
			sql.Named("humidity_percent", r.Humidity_percent),
			sql.Named("total_kg", r.Total_kg),
			sql.Named("cell_front_left_kg", r.CellFrontLeft_kg),
			sql.Named("cell_front_right_kg", r.CellFrontRight_kg),
			sql.Named("cell_back_left_kg", r.CellBackLeft_kg),
			sql.Named("cell_back_right_kg", r.CellBackRight_kg),
		)
		if err != nil {
			return fmt.Errorf("could no store reading (%s,%s): %w", r.LocationID, r.Timestamp, err)
		}

	}

	err = tx.Commit()
	if err != nil {
		return fmt.Errorf("could not commit transaction: %w", err)
	}
	return nil
}

func (s *SQLiteStore) GetScaleHistory(ctx context.Context, locationID string, start, end time.Time) ([]ScaleReading, error) {
	query := `
SELECT
	sensor_id,
	timestamp,
	received_at,
	temperature_c,
	humidity_percent,
	total_weight_kg,
	cell_front_left_kg,
	cell_front_right_kg,
	cell_back_left_kg,
	cell_back_right_kg
FROM scale_readings
WHERE
	location_id = ?
	AND timestamp >= ?
	AND timestamp <= ?
ORDER BY timestamp ASC;
`
	rows, err := s.db.QueryContext(ctx, query, locationID, start.UnixMilli(), end.UnixMilli())
	if err != nil {
		return nil, fmt.Errorf("could not perform query: %w", err)
	}
	defer rows.Close()
	res := make([]ScaleReading, 0)
	for rows.Next() {
		r := ScaleReading{LocationID: locationID}
		var timestamp, receivedAt int64
		if err := rows.Scan(
			&r.SensorID,
			&timestamp,
			&receivedAt,
			&r.Temperature_C,
			&r.Humidity_percent,
			&r.Total_kg,
			&r.CellFrontLeft_kg,
			&r.CellFrontRight_kg,
			&r.CellBackLeft_kg,
			&r.CellBackRight_kg,
		); err != nil {
			return res, fmt.Errorf("could not scan result row: %w", err)
		}
		r.Timestamp = time.UnixMilli(timestamp)
		r.ReceivedAt = time.UnixMilli(receivedAt)
		res = append(res, r)
	}
	err = rows.Err()
	if err != nil {
		err = fmt.Errorf("incomplete read: %w", err)
	}
	return res, err
}

func (s *SQLiteStore) SaveTrafficCount(ctx context.Context, counts []TrafficCount) (err error) {
	query := `
INSERT INTO traffic_count (
	location_id,
	timestamp,
	received_at,
	duration_ms,
	outgoing,
	ingoing
) VALUES (
	:location_id,
	:timestamp,
	:received_at,
	:duration_ms,
	:outgoing,
	:ingoing
)
ON CONFLICT (location_id,timestamp)
DO UPDATE SET
	ingoing = EXCLUDED.ingoing,
	outgoing = EXCLUDED.outgoing;
`
	var locationIDs []string
	locationSet := make(map[string]bool)
	for _, r := range counts {
		if len(r.LocationID) == 0 {
			return fmt.Errorf("empty location_id")
		}
		if locationSet[r.LocationID] == true {
			continue
		}
		locationSet[r.LocationID] = true
		locationIDs = append(locationIDs, r.LocationID)
	}

	tx, err := s.db.BeginTx(ctx, nil)
	if err != nil {
		return fmt.Errorf("could not start transaction: %w", err)
	}
	defer func() {
		if rbErr := tx.Rollback(); rbErr != sql.ErrTxDone {
			err = errors.Join(err, rbErr)
		}
	}()

	for _, locationID := range locationIDs {
		_, err = tx.ExecContext(ctx, "INSERT OR IGNORE INTO locations (location_id) VALUES (?);", locationID)
		if err != nil {
			return fmt.Errorf("could not update locations: %w", err)
		}
	}
	for _, c := range counts {
		_, err = tx.ExecContext(ctx, query,
			sql.Named("location_id", c.LocationID),
			sql.Named("timestamp", c.Timestamp.UnixMilli()),
			sql.Named("received_at", c.ReceivedAt.UnixMilli()),
			sql.Named("duration_ms", c.Duration.Milliseconds()),
			sql.Named("outgoing", c.Outgoing),
			sql.Named("ingoing", c.Ingoing),
		)
		if err != nil {
			return fmt.Errorf("could not save count (%s,%s): %w", c.LocationID, c.Timestamp, err)
		}
	}

	err = tx.Commit()
	if err != nil {
		return fmt.Errorf("could not commit transaction: %w", err)
	}

	return nil
}

func (s *SQLiteStore) GetTrafficHistory(ctx context.Context, locationID string, start, end time.Time) ([]TrafficCount, error) {
	query := `
SELECT
	timestamp,
	received_at,
	duration_ms,
	outgoing,
	ingoing
FROM traffic_count
WHERE
	location_id = ?
	AND timestamp >= ?
	AND timestamp <= ?
ORDER BY timestamp ASC;
`
	rows, err := s.db.QueryContext(ctx, query, locationID, start.UnixMilli(), end.UnixMilli())
	if err != nil {
		return nil, fmt.Errorf("could not perform query: %w", err)
	}
	defer rows.Close()
	res := make([]TrafficCount, 0)
	for rows.Next() {
		c := TrafficCount{LocationID: locationID}
		var timestamp, receivedAt, duration_ms int64
		err = rows.Scan(&timestamp, &receivedAt, &duration_ms, &c.Outgoing, &c.Ingoing)
		if err != nil {
			return res, fmt.Errorf("could not scan row: %w", err)
		}
		c.Timestamp = time.UnixMilli(timestamp)
		c.ReceivedAt = time.UnixMilli(receivedAt)
		c.Duration = time.Duration(duration_ms) * time.Millisecond
		res = append(res, c)
	}

	err = rows.Err()
	if err != nil {
		return res, fmt.Errorf("incomplete read: %w", err)
	}

	return res, nil
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

-- 4. Scale readings
CREATE TABLE IF NOT EXISTS scale_readings (
location_id TEXT NOT NULL,
sensor_id TEXT NOT NULL,
timestamp INTEGER NOT NULL,
received_at INTEGER NOT NULL,
temperature_c REAL,
humidity_percent REAL,
total_weight_kg REAL,
cell_front_left_kg REAL,
cell_front_right_kg REAL,
cell_back_left_kg REAL,
cell_back_right_kg REAL,
PRIMARY KEY (location_id,timestamp),
FOREIGN KEY (location_id) REFERENCES locations(location_id)
) WITHOUT ROWID;

-- 5. Traffic count
CREATE TABLE IF NOT EXISTS traffic_count (
	location_id TEXT    NOT NULL,
	timestamp   INTEGER NOT NULL,
	received_at INTEGER NOT NULL,
	duration_ms INTEGER,
	outgoing INTEGER,
	ingoing INTEGER,
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
