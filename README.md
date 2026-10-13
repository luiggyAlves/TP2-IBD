# TP2 — Introdução a Bancos de Dados (ICC205 — 2026/02)

Implementação do Trabalho Prático II. A especificação completa está em
**[ENUNCIADO.md](ENUNCIADO.md)** e o relatório em
**[docs/tp2_relatorio.pdf](docs/tp2_relatorio.pdf)**.

- **Parte 1**: arquivo de dados paginado organizado por **hashing estático**,
  com h(ID) = ID mod N (N primo), registros de tamanho variável e cadeias de
  overflow por bucket.
- **Parte 2**: **B+Tree em disco** sobre o ID (índice primário) e sobre o Título
  (índice secundário, com chave composta (prefixo de 64 bytes do título, RID)
  para tratar títulos repetidos).

Todo acesso aos arquivos de dados e de índice passa pela camada `blockio`.

## Execução (Docker)

```
docker build -t tp2 .
gzip -dk data/artigo-amostra.csv.gz
docker run --rm -v $(pwd)/data:/data tp2 ./bin/upload /data/artigo-amostra.csv
docker run --rm -v $(pwd)/data:/data tp2 ./bin/findrec 1234
docker run --rm -v $(pwd)/data:/data tp2 ./bin/findtitle "Um título exato"
docker run --rm -v $(pwd)/data:/data tp2 ./bin/findrange 1000 1100
docker run --rm -v $(pwd)/data:/data tp2 ./bin/seek1 1234
docker run --rm -v $(pwd)/data:/data tp2 ./bin/seek2 "Um título exato"
docker run --rm -v $(pwd)/data:/data tp2 ./bin/seekrange 1000 1100
```

Ou pelo Makefile:

```
make docker-build
make docker-run-upload ARGS="/data/artigo-amostra.csv"
make docker-run-findrec ARGS="1234"
make docker-run-seek2 ARGS='"Review of particle physics particle data group"'
```

Com bloco de 8 KB (Seção 7.5): `docker build --build-arg BLOCK_SIZE=8192 -t tp2 .`
e refaça o `upload`. Os arquivos guardam o BLOCK_SIZE com que foram criados, e
os programas recusam abrir um arquivo criado com outro tamanho.

### Construção dos índices

Os dois índices são construídos **dentro do `upload`**, logo depois do arquivo
de dados. Não há comando separado. O `upload` imprime o custo da construção de
cada estrutura (blocos lidos e escritos). Os valores também estão na Seção 7.1.5
do relatório.

## Execução local (sem Docker)

```
make build                                   # binários em bin/ (BLOCK_SIZE=4096)
make BLOCK_SIZE=8192 build                   # recompila tudo com 8 KB
export TP2_DB_DIR=data/db                    # onde ficam os arquivos (padrão: /data/db)
./bin/upload data/artigo-amostra.csv
./bin/findrec 5
TP2_TRACE=1 ./bin/seek2 "Review of particle physics particle data group"   # mostra cada bloco lido
```

| Variável de ambiente | Efeito |
|---|---|
| `TP2_DB_DIR` | diretório dos arquivos de dados e de índice (padrão `/data/db`, o volume do contêiner) |
| `TP2_TRACE=1` | imprime em stderr cada bloco lido nas buscas (bloco, tipo de nó, número de chaves) |

## Arquivos gerados em `/data`

```
/data/
├── artigo-amostra.csv      entrada (só o upload lê; não entra na imagem)
└── db/
    ├── artigos.dat         arquivo de dados por hashing
    ├── idx_id.bpt          índice primário  (B+Tree, chave = ID)
    └── idx_titulo.bpt      índice secundário (B+Tree, chave = prefixo do título + RID)
```

Todos os arquivos são sequências de blocos de BLOCK_SIZE bytes.

**`artigos.dat`**

| blocos | conteúdo |
|---|---|
| 0 | cabeçalho: assinatura, BLOCK_SIZE, N, número de registros, blocos de overflow |
| 1 .. N | bloco primário do bucket b = bloco 1 + b |
| N+1 .. fim | blocos de overflow, encadeados ao seu bucket pelo campo `next` |

Bloco de dados: cabeçalho de 16 bytes (`next`, `bucket`, `count`, `used`),
seguido dos registros colados. Registro: 39 bytes fixos (tamanho, ID, ano,
citações, data/hora de 19 bytes, 3 comprimentos), seguidos de título, autores e
snippet com tamanho variável. RID = (bloco, deslocamento).

**`idx_id.bpt` e `idx_titulo.bpt`**: 1 nó por bloco, e a raiz fica sempre no
bloco 0. Cada nó tem cabeçalho de 32 bytes. A folha guarda pares (chave, RID) e
o ponteiro para a próxima folha. O nó interno guarda n chaves e n+1 ponteiros.

| BLOCK_SIZE | índice | capacidade da folha | ordem do nó interno | altura (amostra) |
|---|---|---|---|---|
| 4096 | primário (chave 4 B) | 406 | 508 | 3 |
| 4096 | secundário (chave 70 B) | 53 | 55 | 4 |
| 8192 | primário | 816 | 1020 | 2 |
| 8192 | secundário | 107 | 111 | 3 |

## Exemplo de entrada e saída

Linha do CSV de entrada:

```
"5";"Design and implementation of an immersive virtual reality system based on a smartphone platform.";"2013";"Anthony Steed|Simon Julier";"12";"2016-10-03 21:10:56";"Design and implementation of ..."
```

Busca pelo hashing (`TP2_DB_DIR=data/db ./bin/findrec 5`; no contêiner, os
caminhos aparecem como `/data/db/...`):

```
== findrec 5 ==
arquivo de dados: data/db/artigos.dat
hashing: h(5) = 5 mod 33151 = bucket 5 (bloco primario 6)
registro encontrado:
  ID          : 5
  Titulo      : Design and implementation of an immersive virtual reality system based on a smartphone platform.
  Ano         : 2013
  Autores     : Anthony Steed|Simon Julier
  Citacoes    : 12
  Atualizacao : 2016-10-03 21:10:56
  Snippet     : Design and implementation of an immersive virtual reality system based on ...
  (RID: bloco 6, deslocamento 16)
blocos lidos no arquivo de dados: 2 (1 cabecalho + 1 do bucket: 1 primario + 0 overflow)
total de blocos do arquivo de dados: 41637
RESUMO findrec id=5 encontrados=1 blocos_lidos_dados=2 total_blocos_dados=41637
```

Mesmo registro pelo índice primário (`./bin/seek1 5`, final da saída):

```
blocos lidos no indice primario : 3
blocos lidos no arquivo de dados: 1
total de blocos lidos           : 4
total de blocos do indice primario : 913
total de blocos do arquivo de dados: 41637
```

Quando não há resultado, os programas imprimem
`AVISO: nenhum registro ... foi encontrado.` e informam os blocos lidos mesmo
assim. Erros de uso (ex.: ID não numérico, intervalo invertido) terminam com
código 2. Erros de arquivo (ex.: `upload` ainda não executado) terminam com
código 1 e uma mensagem `erro: ...`. A última linha `RESUMO ...` de cada
programa reúne os números em formato fácil de extrair por scripts.

## Testes

```
make test               # unitários: blockio, arquivo de dados, B+Tree (~800 mil verificações)
make test-integration   # ponta a ponta: carga da amostra + 7 programas contra um gabarito em Python
make BLOCK_SIZE=8192 test
```

O teste de integração (`tests/integration_test.py`) carrega a amostra num
diretório temporário. Depois compara a saída dos 7 programas com um parser
independente do CSV: consultas oficiais, ~200 IDs sorteados, títulos repetidos
e títulos com prefixos iguais, e intervalos variados. Ele também confere que a
Parte 1 e a Parte 2 devolvem os mesmos registros e verifica invariantes de custo.

## Experimentos e relatório

```
bash scripts/run_experiments.sh     # upload + consultas oficiais com BLOCK_SIZE 4096 e 8192
python3 scripts/gerar_relatorio.py  # regenera docs/tp2_relatorio.pdf a partir dos resultados
```

| Arquivo | Conteúdo |
|---|---|
| `docs/previsao.md` | previsão da Seção 7.2, registrada antes da primeira execução |
| `docs/resultados/upload_<BS>.txt` | saída completa do upload |
| `docs/resultados/consultas_<BS>.txt` | linha RESUMO de cada consulta oficial |
| `docs/resultados/analise_<BS>.txt` | experimentos complementares (`tools/analise.cpp`) |
| `docs/resultados/tabelas.md` | tabelas das Seções 7.3 e 7.5 |
| `docs/tp2_relatorio.pdf` | relatório (o HTML intermediário, `docs/tp2_relatorio.html`, é gerado junto e não é versionado) |

O PDF é gerado com o LibreOffice (`soffice --headless`). Os números do
relatório são lidos desses arquivos e não são digitados à mão.

## Estrutura do código

| Caminho | Papel |
|---|---|
| `include/blockio.hpp`, `src/blockio.cpp` | camada de blocos (template) |
| `include/csv_reader.hpp`, `src/csv_reader.cpp` | leitor do CSV (template) |
| `include/config.hpp` | parâmetros de projeto e caminhos |
| `include/bytes.hpp` | leitura e escrita de inteiros em posições fixas do bloco |
| `include/datafile.hpp`, `src/datafile.cpp` | registro, bloco, hashing, overflow, busca e varredura |
| `include/bptree.hpp`, `src/bptree.cpp` | B+Tree em disco (inserção com divisão, busca, cursor pelas folhas) |
| `src/upload.cpp`, `src/find*.cpp`, `src/seek*.cpp` | os 7 programas |
| `tools/analise.cpp` | experimentos complementares do relatório (`make analise`) |
| `tests/` | testes unitários e de integração |
| `scripts/` | experimentos, tabelas e geração do relatório |

## Entrega

```
git tag v1.0
git push origin v1.0
git ls-remote --tags origin   # confirme que a tag aparece no remoto
```
