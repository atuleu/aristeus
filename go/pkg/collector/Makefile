all: collector check

collector: *.go
	go build

check: *.go
	go test -coverprofile=./cover.out -covermode=atomic
	go-test-coverage --config=./testcoverage.yml
	go vet
