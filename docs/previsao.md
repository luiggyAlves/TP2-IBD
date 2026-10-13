# Previsão antes da medição (Seção 7.2)

Registrada em 12/10/2026, **antes** da primeira execução do `upload` e das
consultas oficiais. Os valores abaixo saem só do layout (código já escrito, mas
não executado) e de modelos analíticos simples. A comparação com o que foi
medido está no relatório (Seção 7.4).

## Entradas do modelo

- n = 204.279 registros (número informado no enunciado).
- Tamanho médio do registro no layout adotado: 39 bytes fixos + título (72,9) +
  autores (40,7) + snippet (377,0) ≈ **529,6 bytes**. As médias dos campos foram
  medidas no CSV.
- Bytes úteis por bloco de dados = BLOCK_SIZE − 16 (cabeçalho do bloco).
- N = ⌈n · 529,6 / (bytes úteis · 0,80)⌉, arredondado para o primo seguinte.
- Número de registros por bucket ~ Poisson(λ = n/N). O bucket transborda se
  receber mais registros do que cabem no bloco: c = ⌊bytes úteis / 529,6⌋.
- B+Tree construída por inserções em ordem aleatória de chave (a ordem física
  do arquivo hash). A ocupação média esperada dos nós é ln 2 ≈ 69 % (Yao, 1978).

## Parâmetros previstos

| Grandeza                                  | 4096 B        | 8192 B        |
|-------------------------------------------|---------------|---------------|
| registros por bloco (registro médio)      | 7,7 → 7       | 15,4 → 15     |
| N (buckets)                               | ≈ 33.149      | ≈ 16.547      |
| λ = registros por bucket                  | 6,16          | 12,35         |
| P(bucket transbordar) = P(X ≥ c+1)        | 0,28          | 0,18          |
| blocos de overflow ≈ N · P                | ≈ 9.240       | ≈ 3.010       |
| total de blocos do arquivo de dados       | ≈ 42.390      | ≈ 19.550      |
| índice primário: folha / ordem do interno | 406 / 508     | 816 / 1020    |
| índice primário: folhas (69 %)            | ≈ 726         | ≈ 361         |
| índice primário: altura                   | **3**         | **2**         |
| índice secundário: folha / ordem          | 53 / 55       | 107 / 111     |
| índice secundário: folhas (69 %)          | ≈ 5.560       | ≈ 2.750       |
| índice secundário: altura                 | **4**         | **3**         |

Com folhas 100 % cheias, o índice primário de 4096 B teria 504 folhas e caberia
sob uma raiz de ordem 508 (altura 2). Com inserção aleatória (≈ 69 %), ele precisa
de ≈ 726 folhas. Por isso a previsão é de altura 3.

## Blocos lidos previstos por operação (BLOCK_SIZE = 4096)

| Operação | Caso | Índice | Dados |
|---|---|---|---|
| findrec | ID existente | – | 2 (cabeçalho + bucket). Será 3 se o registro estiver no overflow (≈ 10 % dos registros). |
| findrec | ID inexistente | – | 1 + comprimento da cadeia: 2 (72 %) ou 3 (28 %) |
| findtitle | qualquer título | – | todos os blocos ≈ 42.390 |
| findrange | qualquer intervalo | – | todos os blocos ≈ 42.390 |
| seek1 | ID existente | 3 | 1 |
| seek1 | ID inexistente | 3 | 0 |
| seek2 | título único | 4 (ou 5 se cruzar a fronteira de uma folha) | 1 |
| seek2 | título com k repetições | 4 + ⌈k/36,7⌉ − 1 (podendo ser +1) | k (45 e 26) |
| seek2 | título inexistente | 4 (ou 5) | 0 |
| seekrange | [1, 50] (10 registros) | 3 | 10 |
| seekrange | [1000, 1100] (20) | 3 | 20 |
| seekrange | [500000, 500500] (99) | 3 ou 4 | 99 |
| seekrange | [200000, 260000] (3.052) | 3 + ⌈3052/281⌉ ≈ 14 | 3.052 |
| seekrange | [1600000, 1700000] (0) | 3 | 0 |

A quantidade de registros de cada intervalo foi contada no CSV.

Com 8192 B, os custos previstos são: findrec 2; seek1 2 + 1 = 3; seek2 3 + 1;
varreduras ≈ 19.550 blocos; seekrange [200000, 260000] ≈ 2 + ⌈3052/566⌉ ≈ 8 no
índice.

## Custo previsto de construção (4096 B)

- Arquivo de dados: N escritas para criar os buckets vazios, mais 1 leitura e
  1 escrita por registro, mais 1 escrita extra por bloco de overflow. Total
  previsto: ≈ 204 mil leituras e ≈ 247 mil escritas.
- Índice primário: ≈ altura leituras por inserção (≈ 2,9 em média, porque a
  árvore cresce durante a carga), ou seja, ≈ 590 mil leituras. Escritas:
  ≈ 1 por inserção, mais 2 por divisão, ou seja, ≈ 206 mil escritas.
- Índice secundário: ≈ 3,9 leituras por inserção (≈ 800 mil). Escritas:
  ≈ 204 mil + 2 × 5.600 divisões ≈ 216 mil.
