package main

import (
	"bytes"
	"testing"

	"github.com/stretchr/testify/assert"
)

func TestParseStats(t *testing.T) {
	assert := assert.New(t)

	reader := bytes.NewBuffer([]byte(procStatExample))
	cores, err := parseProcStats(reader)
	assert.NoError(err)
	assert.Equal(map[string]CpuStats{
		"total": CpuStats{
			Total: 6457554 + 18803880 + 15216709 + 3960896998 + 103958559 + 293411,
			Idle:  3960896998 + 103958559,
		},
		"core0": CpuStats{Total: 190199 + 304011 + 489899 + 126524478 + 161626 + 117712, Idle: 126524478 + 161626},
		"core1": CpuStats{Total: 54863 + 114481 + 191771 + 128021912 + 37597 + 15791, Idle: 128021912 + 37597},
		"core2": CpuStats{Total: 92105 + 162102 + 286404 + 127753998 + 51896 + 15824, Idle: 127753998 + 51896},
		"core3": CpuStats{Total: 39023 + 87459 + 139761 + 128202918 + 17902 + 618, Idle: 128202918 + 17902},
	}, cores)
}

func TestParseMeminfo(t *testing.T) {
	assert := assert.New(t)
	reader := bytes.NewBuffer([]byte(meminfoExample))

	meminfo, err := parseProcMeminfo(reader)
	assert.NoError(err)
	assert.Equal(MemoryStats{
		MemTotalBytes:     131010068 * 1024,
		MemAvailableBytes: 103161024 * 1024,
		SwapTotalBytes:    16777212 * 1024,
		SwapFreeBytes:     16777196 * 1024,
	}, meminfo)
}

var procStatExample = `cpu  6457554 18803880 15216709 3960896998 103958559 0 293411 0 0 0
cpu0 190199 304011 489899 126524478 161626 0 117712 0 0 0
cpu1 54863 114481 191771 128021912 37597 0 15791 0 0 0
cpu2 92105 162102 286404 127753998 51896 0 15824 0 0 0
cpu3 39023 87459 139761 128202918 17902 0 618 0 0 0
// truncated
ctxt 10096214126
btime 1788436841
processes 20326884
procs_running 2
procs_blocked 3
softirq 3741212737 4060766 883087636 1759823 755533706 1290453 0 211014605 1177506670 167 706958911
`
var meminfoExample = `MemTotal:       131010068 kB
MemFree:        47639044 kB
MemAvailable:   103161024 kB
Buffers:         2731576 kB
Cached:         56498056 kB
SwapCached:            0 kB
Active:         13418224 kB
Inactive:       57363628 kB
Active(anon):   11760832 kB
Inactive(anon):    38108 kB
Active(file):    1657392 kB
Inactive(file): 57325520 kB
Unevictable:        7532 kB
Mlocked:            7532 kB
SwapTotal:      16777212 kB
SwapFree:       16777196 kB
Zswap:                 0 kB
Zswapped:              0 kB
Dirty:               324 kB
Writeback:             0 kB
AnonPages:      11558076 kB
Mapped:          2317708 kB
Shmem:            241492 kB
KReclaimable:    7542080 kB
Slab:            8409156 kB
SReclaimable:    7542080 kB
SUnreclaim:       867076 kB
KernelStack:       54976 kB
PageTables:       116744 kB
SecPageTables:     13384 kB
NFS_Unstable:          0 kB
Bounce:                0 kB
WritebackTmp:          0 kB
CommitLimit:    82282244 kB
Committed_AS:   26787664 kB
VmallocTotal:   13743895347199 kB
VmallocUsed:      225504 kB
VmallocChunk:          0 kB
Percpu:            48896 kB
HardwareCorrupted:     0 kB
AnonHugePages:     36864 kB
ShmemHugePages:        0 kB
ShmemPmdMapped:        0 kB
FileHugePages:    407552 kB
FilePmdMapped:     24576 kB
CmaTotal:              0 kB
CmaFree:         4137960 kB
Unaccepted:            0 kB
Balloon:               0 kB
HugePages_Total:       0
HugePages_Free:        0
HugePages_Rsvd:        0
HugePages_Surp:        0
Hugepagesize:       2048 kB
Hugetlb:               0 kB
DirectMap4k:    10794744 kB
DirectMap2M:    93169664 kB
DirectMap1G:    29360128 kB
`
