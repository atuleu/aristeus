all: arisble check

arisble: *.go
	go build

check: *.go
	gotestsum --format dots --  -coverprofile=./cover.out -covermode=atomic
	go vet
