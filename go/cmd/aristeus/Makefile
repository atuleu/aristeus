all: aristeus check

aristeus: *.go
	go build

check: *.go
	gotestsum --format dots
	go vet

clean:
	rm aristeus

.PHONY: aristeus
