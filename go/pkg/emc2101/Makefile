all: emc2101 check

emc2101:
	go build

generate:
	go generate

check:
	gotestsum --format testname -- -race -coverprofile=./cover.out -covermode=atomic
	go vet
