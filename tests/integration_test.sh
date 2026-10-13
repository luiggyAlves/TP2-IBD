#!/usr/bin/env bash
# Teste de ponta a ponta: carrega a amostra oficial num diretorio temporario e
# confere os 7 programas contra um gabarito independente (tests/integration_test.py).
#
# Uso: bash tests/integration_test.sh [arquivo.csv]
#      (padrao: data/artigo-amostra.csv; os binarios devem estar em bin/)
set -euo pipefail
cd "$(dirname "$0")/.."

CSV="${1:-data/artigo-amostra.csv}"
if [[ ! -f "$CSV" ]]; then
    echo "CSV nao encontrado: $CSV (rode: gzip -dk data/artigo-amostra.csv.gz)" >&2
    exit 1
fi
python3 tests/integration_test.py "$CSV" bin
