package collector

import (
	"os"
	"path/filepath"
	"testing"
	"time"

	"github.com/stretchr/testify/suite"
)

type CollectorConfigSuite struct {
	suite.Suite
	tmpdir string
}

func (s *CollectorConfigSuite) SetupTest() {
	s.tmpdir = s.T().TempDir()
}

func (s *CollectorConfigSuite) TestYAMLNonExistentIsNotError() {
	cfg, err := NewCollectorConfigFromFiles(filepath.Join(s.tmpdir, "does-not-exists.yml"))
	s.Assert().NoError(err)
	s.Assert().Equal(defaultCollectorConfig(), cfg)
}

func (s *CollectorConfigSuite) TestYAMLOnlyOverridesDefined() {
	paths := []string{filepath.Join(s.tmpdir, "configA.yaml"), filepath.Join(s.tmpdir, "configB.yaml")}
	s.Require().NoError(os.WriteFile(paths[0], []byte(`
connection-jitter: 1s
`), 0644))
	s.Require().NoError(os.WriteFile(paths[1], []byte(`
janitor-schedule:
  hour: 2
  minute: 2
`), 0644))

	cfg, err := NewCollectorConfigFromFiles(filepath.Join(s.tmpdir, "does-not-exists.yml"), paths[0], paths[1])
	s.Assert().NoError(err)
	expected := defaultCollectorConfig()
	expected.ConnectionJitter = 1 * time.Second
	expected.JanitorTime.Hour = 2
	expected.JanitorTime.Minute = 2

	s.Assert().Equal(expected, cfg)
}

func (s *CollectorConfigSuite) TestHiveID() {
	path := filepath.Join(s.tmpdir, "config.yaml")
	s.Require().NoError(os.WriteFile(path, []byte(`hive-ids:
  - 1
  - 3
  - 13
`), 0644))

	cfg, err := NewCollectorConfigFromFiles(path)
	s.Assert().NoError(err)
	expected := defaultCollectorConfig()
	expected.HiveIDFilter = HiveIDSet{1: true, 3: true, 13: true}
	s.Assert().Equal(expected, cfg)
}

func (s *CollectorConfigSuite) TestScaleMapping() {
	path := filepath.Join(s.tmpdir, "config.yaml")
	s.Require().NoError(os.WriteFile(path, []byte(`scales:
  - address: 02:02:02:02:02:02
    hive-id: 1
  - address: 02:02:02:02:03:04
    hive-id: 13
`), 0644))

	cfg, err := NewCollectorConfigFromFiles(path)
	s.Assert().NoError(err)
	expected := defaultCollectorConfig()
	expected.ScaleAddresses = ScaleAddressMap{"02:02:02:02:02:02": 1, "02:02:02:02:03:04": 13}
	s.Assert().Equal(expected, cfg)
}

func (s *CollectorConfigSuite) TestScaleMappingError() {
	path := filepath.Join(s.tmpdir, "config.yaml")
	s.Require().NoError(os.WriteFile(path, []byte(`scales:
  - address: 02:02:02:02:02:02
    hive-id: 1
  - address: 02:02:02:02:03:04
    hive-id: 1
`), 0644))

	cfg, err := NewCollectorConfigFromFiles(path)
	s.Assert().ErrorContains(err, "Hive ID 1 is mapped multiple times")
	s.Assert().Equal(cfg, CollectorConfig{})

	s.Require().NoError(os.WriteFile(path, []byte(`scales:
  - address: 02:02:02:02:02:02
    hive-id: 1
  - address: 02:02:02:02:02:02
    hive-id: 13
`), 0644))
	cfg, err = NewCollectorConfigFromFiles(path)
	s.Assert().ErrorContains(err, "address `02:02:02:02:02:02` is mapped multiple times")
	s.Assert().Equal(cfg, CollectorConfig{})

}

func TestCollectorConfigSuite(t *testing.T) {
	suite.Run(t, new(CollectorConfigSuite))
}
