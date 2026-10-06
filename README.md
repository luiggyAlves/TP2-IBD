# TP2 — Introdução a Bancos de Dados (ICC205 — 2026/02)

Repositório base do Trabalho Prático II. A especificação completa está em
**[ENUNCIADO.md](ENUNCIADO.md)**.

O trabalho é **individual**. Crie seu repositório a partir deste template
(botão *Use this template*), mantenha-o **privado**, adicione o professor
(`altigran`) como colaborador e marque a versão final com a tag `v1.0`:

```
git tag v1.0
git push origin v1.0
git ls-remote --tags origin   # confirme que a tag aparece no remoto
```

Prazo de entrega: **10/10/2026, 23:59**.

## O que já vem pronto

| Arquivo | Papel |
|---|---|
| `include/blockio.hpp`, `src/blockio.cpp` | camada de acesso a blocos; **único** caminho admitido de leitura e escrita nos arquivos de dados e de índice |
| `include/csv_reader.hpp`, `src/csv_reader.cpp` | leitor do CSV de entrada, usado apenas na carga inicial |
| `include/record.hpp` | struct `Article`, como o registro chega do CSV |
| `Makefile`, `Dockerfile`, `docker-compose.yml` | compilação e execução |

## O que você implementa

| Programa | Parte | O que faz |
|---|---|---|
| `upload <arquivo.csv>` | 1 | carga inicial, criando o arquivo de dados por hashing |
| `findrec <ID>` | 1 | busca por ID no arquivo de dados |
| `findtitle "<Título>"` | 1 | busca por título, varrendo o arquivo sequencialmente |
| `findrange <ini> <fim>` | 1 | busca por intervalo de IDs, varrendo o arquivo |
| `seek1 <ID>` | 2 | busca por ID pelo índice primário (B+Tree) |
| `seek2 "<Título>"` | 2 | busca por título pelo índice secundário (B+Tree) |
| `seekrange <ini> <fim>` | 2 | busca por intervalo pelas folhas encadeadas do índice primário |

Os fontes em `src/` são esqueletos que compilam e retornam código 1 até serem
implementados.

## Compilar e executar

```
make build                       # binários em bin/
make BLOCK_SIZE=8192 build       # recompila com bloco de 8 KB (Seção 7.5)

make docker-build
make docker-run-upload ARGS="/data/artigo-amostra.csv"
make docker-run-findrec ARGS="1234"
```

Ou diretamente:

```
docker build -t tp2 .
docker run --rm -v $(pwd)/data:/data tp2 ./bin/upload /data/artigo-amostra.csv
docker run --rm -v $(pwd)/data:/data tp2 ./bin/seek1 1234
```

Antes da primeira carga, coloque a amostra oficial em `data/` e descompacte-a
(`gzip -dk data/artigo-amostra.csv.gz`). Veja [data/README.md](data/README.md).

## Medição

A métrica do trabalho é **contagem de blocos lógicos**, não tempo. O contador de
`blockio::BlockFile::stats()` é incrementado a cada leitura ou escrita solicitada
à camada, o que torna o resultado idêntico em qualquer máquina. Acessos que
contornem a camada (`fstream`, `mmap` e equivalentes) invalidam a medição.
