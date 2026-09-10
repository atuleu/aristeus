all: collector check

collector: *.go
	go build

check: *.go
	go test -race -coverprofile=./cover.out -covermode=atomic
	go-test-coverage --config=./testcoverage.yml
	go vet
