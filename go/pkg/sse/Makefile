all: sse check

sse: *.go
	go build

check: *.go
	gotestsum --format testname --  -coverprofile=./cover.out -covermode=atomic
	go vet
