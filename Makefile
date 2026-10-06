# TP2 — Bancos de Dados I (IBD 2026/02)
#
# Alvos principais:
#   make build                 compila os binarios em bin/
#   make BLOCK_SIZE=8192 build recompila com bloco de 8 KB (Secao 7.5)
#   make docker-build          constroi a imagem Docker
#   make docker-run-<prog>     executa <prog> dentro do contendor

CXX      ?= g++
BLOCK_SIZE ?= 4096
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -Iinclude -DBLOCK_SIZE=$(BLOCK_SIZE)

IMAGE    ?= tp2
DATA_DIR ?= $(shell pwd)/data

PROGS   := upload findrec findtitle findrange seek1 seek2 seekrange
COMMON  := src/blockio.cpp src/csv_reader.cpp
BINS    := $(addprefix bin/,$(PROGS))

.PHONY: build clean docker-build $(addprefix docker-run-,$(PROGS))

build: $(BINS)

bin/%: src/%.cpp $(COMMON) | bin
	$(CXX) $(CXXFLAGS) $< $(COMMON) -o $@

bin:
	mkdir -p bin

clean:
	rm -f bin/*

docker-build:
	docker build -t $(IMAGE) .

# Uso: make docker-run-findrec ARGS="1234"
$(addprefix docker-run-,$(PROGS)): docker-run-%:
	docker run --rm -v $(DATA_DIR):/data $(IMAGE) ./bin/$* $(ARGS)

.PHONY: test
test: | bin
	$(CXX) $(CXXFLAGS) tests/smoke_blockio.cpp src/blockio.cpp -o bin/smoke_blockio
	./bin/smoke_blockio
