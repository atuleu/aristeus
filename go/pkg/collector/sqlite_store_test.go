package collector

import (
	"context"
	"os"
	"path/filepath"
	"testing"

	"github.com/stretchr/testify/suite"
)

type SQLiteStoreSuite struct {
	DataJournalSuite
	tmpDir string
}

func (s *SQLiteStoreSuite) SetupSuite() {
	s.tmpDir = s.T().TempDir()
	s.SetupStore = func(ctx context.Context) (DataJournal, error) {
		dbPath := filepath.Join(s.tmpDir, s.T().Name()+".db")
		err := os.MkdirAll(filepath.Dir(dbPath), 0755)
		if err != nil {
			return nil, err
		}
		return NewSQLiteStore(ctx, dbPath)
	}
}

func TestSQLiteStoreSuite(t *testing.T) {
	suite.Run(t, new(SQLiteStoreSuite))
}
