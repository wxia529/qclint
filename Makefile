GO ?= go
PREFIX ?= /usr/local
DESTDIR ?=

.PHONY: all build test vet fuzz install clean

all: build

build:
	$(GO) build -trimpath -o build/qclint ./cmd/qclint

test:
	$(GO) test ./...

vet:
	$(GO) vet ./...

fuzz:
	$(GO) test ./qclint -run=^$$ -fuzz=FuzzParsers -fuzztime=30s

install: build
	install -Dm755 build/qclint "$(DESTDIR)$(PREFIX)/bin/qclint"

clean:
	$(GO) clean -testcache
	rm -f build/qclint
	rmdir build 2>/dev/null || true
