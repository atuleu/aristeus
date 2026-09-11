all: collector check

collector: *.go
	go build

check: *.go
	gotestsum --format dots -- -race -coverprofile=./cover.out -covermode=atomic
	go vet
