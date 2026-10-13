# TP2 — Bancos de Dados I (IBD 2026/02)
#
# Alvos principais:
#   make build                 compila os binarios em bin/
#   make BLOCK_SIZE=8192 build recompila com bloco de 8 KB (Secao 7.5)
#   make docker-build          constroi a imagem Docker
#   make docker-run-<prog>     executa <prog> dentro do contendor
#   make test                  testes unitarios (blockio, arquivo de dados, B+Tree)
#   make test-integration      testes de ponta a ponta sobre a amostra oficial
#   make experiments           consultas oficiais com BLOCK_SIZE 4096 e 8192

CXX      ?= g++
BLOCK_SIZE ?= 4096
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -Iinclude -DBLOCK_SIZE=$(BLOCK_SIZE)

IMAGE    ?= tp2
DATA_DIR ?= $(shell pwd)/data

PROGS   := upload findrec findtitle findrange seek1 seek2 seekrange
COMMON  := src/blockio.cpp src/csv_reader.cpp src/datafile.cpp src/bptree.cpp
HEADERS := $(wildcard include/*.hpp)
BINS    := $(addprefix bin/,$(PROGS))

# Marca do BLOCK_SIZE usado na ultima compilacao: trocar o BLOCK_SIZE
# (ex.: make BLOCK_SIZE=8192 build) forca a recompilacao de todos os binarios.
BS_STAMP := bin/.block_size_$(BLOCK_SIZE)

.PHONY: build clean docker-build $(addprefix docker-run-,$(PROGS))

build: $(BINS)

bin/%: src/%.cpp $(COMMON) $(HEADERS) $(BS_STAMP) | bin
	$(CXX) $(CXXFLAGS) $< $(COMMON) -o $@

$(BS_STAMP): | bin
	rm -f bin/.block_size_*
	touch $@

bin:
	mkdir -p bin

clean:
	rm -f bin/* bin/.block_size_*

docker-build:
	docker build -t $(IMAGE) .

# Uso: make docker-run-findrec ARGS="1234"
$(addprefix docker-run-,$(PROGS)): docker-run-%:
	docker run --rm -v $(DATA_DIR):/data $(IMAGE) ./bin/$* $(ARGS)

.PHONY: test test-integration experiments analise

# Experimentos complementares do relatorio (nao e um dos 7 programas pedidos).
analise: bin/analise
bin/analise: tools/analise.cpp $(COMMON) $(HEADERS) $(BS_STAMP) | bin
	$(CXX) $(CXXFLAGS) $< $(COMMON) -o $@

test: | bin
	$(CXX) $(CXXFLAGS) tests/smoke_blockio.cpp src/blockio.cpp -o bin/smoke_blockio
	./bin/smoke_blockio
	$(CXX) $(CXXFLAGS) tests/test_datafile.cpp $(COMMON) -o bin/test_datafile
	./bin/test_datafile
	$(CXX) $(CXXFLAGS) tests/test_bptree.cpp $(COMMON) -o bin/test_bptree
	./bin/test_bptree

# Compila, carrega a amostra oficial em data/db e confere os 7 programas
# entre si e contra uma referencia independente (requer python3).
test-integration: build
	bash tests/integration_test.sh

# Roda as consultas oficiais com BLOCK_SIZE=4096 e 8192 (Secoes 7.3 e 7.5).
experiments:
	bash scripts/run_experiments.sh
