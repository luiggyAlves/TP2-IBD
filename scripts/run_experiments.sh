#!/usr/bin/env bash
# Executa a carga e TODAS as consultas oficiais (queries/oficial.txt) para cada
# BLOCK_SIZE pedido (padrao: 4096 e 8192) e grava os resultados em
# docs/resultados/:
#
#   upload_<BS>.txt     saida completa do upload (estatisticas da carga)
#   consultas_<BS>.txt  uma linha RESUMO por execucao de programa
#   tabelas.md          tabelas da Secao 7.3/7.5 (gerada por scripts/tabelas.py)
#
# Os arquivos de dados/indice de cada BLOCK_SIZE ficam em data/db_<BS>.
# Ao final os binarios sao recompilados com BLOCK_SIZE=4096 (padrao).
#
# Uso: bash scripts/run_experiments.sh [BLOCK_SIZE ...]
#      CSV=/caminho/outro.csv bash scripts/run_experiments.sh 4096
set -euo pipefail
cd "$(dirname "$0")/.."

CSV="${CSV:-data/artigo-amostra.csv}"
OUT=docs/resultados
SIZES=("$@")
[[ ${#SIZES[@]} -eq 0 ]] && SIZES=(4096 8192)
mkdir -p "$OUT"

if [[ ! -f "$CSV" ]]; then
    echo "CSV nao encontrado: $CSV (rode: gzip -dk data/artigo-amostra.csv.gz)" >&2
    exit 1
fi

for BS in "${SIZES[@]}"; do
    echo "=== BLOCK_SIZE=$BS ==="
    make -s BLOCK_SIZE="$BS" build
    export TP2_DB_DIR="data/db_$BS"

    ./bin/upload "$CSV" > "$OUT/upload_$BS.txt"
    grep '^RESUMO' "$OUT/upload_$BS.txt"

    RES="$OUT/consultas_$BS.txt"
    : > "$RES"
    # Cada linha util do arquivo oficial: 'id N', 'titulo "..."' ou 'intervalo A B'.
    while read -r tipo resto; do
        case "$tipo" in
            id)
                ./bin/findrec "$resto" | grep '^RESUMO' >> "$RES"
                ./bin/seek1 "$resto" | grep '^RESUMO' >> "$RES"
                ;;
            titulo)
                t="${resto#\"}"; t="${t%\"}"          # remove as aspas externas
                echo "$(./bin/findtitle "$t" | grep '^RESUMO') titulo=\"$t\"" >> "$RES"
                echo "$(./bin/seek2 "$t" | grep '^RESUMO') titulo=\"$t\"" >> "$RES"
                ;;
            intervalo)
                read -r a b <<< "$resto"
                ./bin/findrange "$a" "$b" | grep '^RESUMO' >> "$RES"
                ./bin/seekrange "$a" "$b" | grep '^RESUMO' >> "$RES"
                ;;
            *) echo "linha ignorada: $tipo $resto" >&2 ;;
        esac
    done < <(grep -vE '^[[:space:]]*(#|$)' queries/oficial.txt)
    echo "$(wc -l < "$RES") execucoes registradas em $RES"

    # Experimentos complementares (custos medios, intervalo x custo, ordem de insercao).
    make -s BLOCK_SIZE="$BS" analise
    ./bin/analise > "$OUT/analise_$BS.txt"
    echo "analise gravada em $OUT/analise_$BS.txt"
done

make -s build  # volta ao BLOCK_SIZE padrao (4096)
if command -v python3 > /dev/null; then
    python3 scripts/tabelas.py > "$OUT/tabelas.md"
    echo "tabelas em $OUT/tabelas.md"
fi
