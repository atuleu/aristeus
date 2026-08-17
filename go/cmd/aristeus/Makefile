all: aristeus check

aristeus: *.go
	go build

check: *.go
	go test
	go vet

clean:
	rm aristeus
