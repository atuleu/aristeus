all: emc2101 check

emc2101:
	go build

check:
	gotestsum --format testname -- -race -coverprofile=./cover.out -covermode=atomic
	go vet
