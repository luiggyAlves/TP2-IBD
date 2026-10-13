# Resultados das consultas oficiais

Gerado por `scripts/tabelas.py` a partir de `docs/resultados/consultas_<BS>.txt`.

## BLOCK_SIZE = 4096

Totais: arquivo de dados = 41.637 blocos; índice primário = 913 blocos (altura 3); índice secundário = 5.701 blocos (altura 4).

### Busca por ID

| ID | existe | findrec (dados) | seek1 (índice) | seek1 (dados) | seek1 (total) |
|---|---|---|---|---|---|
| 5 | sim | 2 | 3 | 1 | 4 |
| 316639 | sim | 2 | 3 | 1 | 4 |
| 663752 | sim | 2 | 3 | 1 | 4 |
| 1014283 | sim | 2 | 3 | 1 | 4 |
| 1483294 | sim | 2 | 3 | 1 | 4 |
| 1 | não | 3 | 3 | 0 | 3 |
| 2000000 | não | 2 | 3 | 0 | 3 |

### Busca por título

| título | registros | findtitle (dados) | seek2 (índice) | seek2 (dados) | seek2 (total) |
|---|---|---|---|---|---|
| Design and implementation of an immersive virtual reality... | 1 | 41.637 | 5 | 1 | 6 |
| A rotating-magnet based wireless electrical power transfe... | 1 | 41.637 | 4 | 1 | 5 |
| Review of particle physics particle data group | 45 | 41.637 | 6 | 45 | 51 |
| Two-dimensional gas of massless Dirac fermions in graphene | 26 | 41.637 | 5 | 26 | 31 |
| Um titulo que nao existe na base | 0 | 41.637 | 4 | 0 | 4 |

### Busca por intervalo

| intervalo | registros | findrange (dados) | seekrange (índice) | seekrange (dados) | seekrange (total) |
|---|---|---|---|---|---|
| [1, 50] | 10 | 41.637 | 3 | 10 | 13 |
| [1000, 1100] | 20 | 41.637 | 4 | 20 | 24 |
| [500000, 500500] | 99 | 41.637 | 3 | 99 | 102 |
| [200000, 260000] | 3.052 | 41.637 | 18 | 3.052 | 3.070 |
| [1600000, 1700000] | 0 | 41.637 | 3 | 0 | 3 |

## BLOCK_SIZE = 8192

Totais: arquivo de dados = 19.298 blocos; índice primário = 448 blocos (altura 2); índice secundário = 2.807 blocos (altura 3).

### Busca por ID

| ID | existe | findrec (dados) | seek1 (índice) | seek1 (dados) | seek1 (total) |
|---|---|---|---|---|---|
| 5 | sim | 2 | 2 | 1 | 3 |
| 316639 | sim | 2 | 2 | 1 | 3 |
| 663752 | sim | 2 | 2 | 1 | 3 |
| 1014283 | sim | 2 | 2 | 1 | 3 |
| 1483294 | sim | 2 | 2 | 1 | 3 |
| 1 | não | 2 | 2 | 0 | 2 |
| 2000000 | não | 2 | 2 | 0 | 2 |

### Busca por título

| título | registros | findtitle (dados) | seek2 (índice) | seek2 (dados) | seek2 (total) |
|---|---|---|---|---|---|
| Design and implementation of an immersive virtual reality... | 1 | 19.298 | 3 | 1 | 4 |
| A rotating-magnet based wireless electrical power transfe... | 1 | 19.298 | 3 | 1 | 4 |
| Review of particle physics particle data group | 45 | 19.298 | 3 | 45 | 48 |
| Two-dimensional gas of massless Dirac fermions in graphene | 26 | 19.298 | 3 | 26 | 29 |
| Um titulo que nao existe na base | 0 | 19.298 | 3 | 0 | 3 |

### Busca por intervalo

| intervalo | registros | findrange (dados) | seekrange (índice) | seekrange (dados) | seekrange (total) |
|---|---|---|---|---|---|
| [1, 50] | 10 | 19.298 | 2 | 10 | 12 |
| [1000, 1100] | 20 | 19.298 | 2 | 20 | 22 |
| [500000, 500500] | 99 | 19.298 | 2 | 99 | 101 |
| [200000, 260000] | 3.052 | 19.298 | 9 | 3.052 | 3.061 |
| [1600000, 1700000] | 0 | 19.298 | 2 | 0 | 2 |

