all: aristeus-node-monitor check



aristeus-node-monitor: *.go
	go build

check: *.go
	gotestsum --format testname -- -race -coverprofile=./cover.out -covermode=atomic
	go vet

clean:
	rm aristeus-node-monitor

.PHONY: aristeus-node-monitor
