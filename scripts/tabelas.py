#!/usr/bin/env python3
"""Monta as tabelas de resultados a partir de docs/resultados/.

Lê as linhas RESUMO gravadas por scripts/run_experiments.sh e imprime, em
Markdown, as tabelas usadas nas Seções 7.3 e 7.5 do relatório.
Também é importado por scripts/gerar_relatorio.py (função carregar()).

Uso: python3 scripts/tabelas.py > docs/resultados/tabelas.md
"""

import os
import re
import sys

RAIZ = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')
DIR = os.path.join(RAIZ, 'docs', 'resultados')
KV = re.compile(r'(\w+)=("[^"]*"|\S+)')


def parse_resumo(linha):
    """'RESUMO prog k=v k="v com espaco" ...' -> (prog, {k: v})."""
    partes = linha.split(maxsplit=2)
    prog, resto = partes[1], partes[2] if len(partes) > 2 else ''
    d = {}
    for k, v in KV.findall(resto):
        v = v.strip('"')
        d[k] = int(v) if re.fullmatch(r'-?\d+', v) else v
    return prog, d


def carregar(bs):
    """Resultados de um BLOCK_SIZE: dicionário com upload, ids, titulos, faixas."""
    up = None
    with open(os.path.join(DIR, f'upload_{bs}.txt'), encoding='utf-8') as f:
        texto_upload = f.read()
    for linha in texto_upload.splitlines():
        if linha.startswith('RESUMO'):
            up = parse_resumo(linha)[1]
    res = {'upload': up, 'upload_texto': texto_upload, 'ids': {}, 'titulos': {}, 'faixas': {}}
    with open(os.path.join(DIR, f'consultas_{bs}.txt'), encoding='utf-8') as f:
        for linha in f:
            if not linha.startswith('RESUMO'):
                continue
            prog, d = parse_resumo(linha)
            if prog in ('findrec', 'seek1'):
                res['ids'].setdefault(d['id'], {})[prog] = d
            elif prog in ('findtitle', 'seek2'):
                res['titulos'].setdefault(d['titulo'], {})[prog] = d
            else:
                res['faixas'].setdefault((d['ini'], d['fim']), {})[prog] = d
    return res


def fmt(n):
    """Número com separador de milhar no padrão brasileiro."""
    return f'{n:,}'.replace(',', '.')


def tabela(cab, linhas):
    out = ['| ' + ' | '.join(cab) + ' |', '|' + '|'.join('---' for _ in cab) + '|']
    out += ['| ' + ' | '.join(str(c) for c in l) + ' |' for l in linhas]
    return '\n'.join(out)


def tabelas_md(bs, r):
    u = r['upload']
    s = [f'## BLOCK_SIZE = {bs}', '',
         f"Totais: arquivo de dados = {fmt(u['blocos_dados'])} blocos; "
         f"índice primário = {fmt(u['idx1_blocos'])} blocos (altura {u['idx1_altura']}); "
         f"índice secundário = {fmt(u['idx2_blocos'])} blocos (altura {u['idx2_altura']}).", '']
    linhas = []
    for i, d in r['ids'].items():
        f, s1 = d['findrec'], d['seek1']
        linhas.append([i, 'sim' if f['encontrados'] else 'não', f['blocos_lidos_dados'], s1['blocos_lidos_indice'],
                       s1['blocos_lidos_dados'], s1['blocos_lidos_indice'] + s1['blocos_lidos_dados']])
    s += ['### Busca por ID', '', tabela(['ID', 'existe', 'findrec (dados)', 'seek1 (índice)', 'seek1 (dados)',
                                          'seek1 (total)'], linhas), '']
    linhas = []
    for t, d in r['titulos'].items():
        f, s2 = d['findtitle'], d['seek2']
        linhas.append([t if len(t) < 60 else t[:57] + '...', f['encontrados'], fmt(f['blocos_lidos_dados']),
                       s2['blocos_lidos_indice'], s2['blocos_lidos_dados'],
                       s2['blocos_lidos_indice'] + s2['blocos_lidos_dados']])
    s += ['### Busca por título', '', tabela(['título', 'registros', 'findtitle (dados)', 'seek2 (índice)',
                                              'seek2 (dados)', 'seek2 (total)'], linhas), '']
    linhas = []
    for (a, b), d in r['faixas'].items():
        f, sr = d['findrange'], d['seekrange']
        linhas.append([f'[{a}, {b}]', fmt(f['encontrados']), fmt(f['blocos_lidos_dados']), sr['blocos_lidos_indice'],
                       fmt(sr['blocos_lidos_dados']), fmt(sr['blocos_lidos_indice'] + sr['blocos_lidos_dados'])])
    s += ['### Busca por intervalo', '', tabela(['intervalo', 'registros', 'findrange (dados)', 'seekrange (índice)',
                                                 'seekrange (dados)', 'seekrange (total)'], linhas), '']
    return '\n'.join(s)


def main():
    tamanhos = sorted(int(m.group(1)) for m in
                      (re.match(r'upload_(\d+)\.txt$', n) for n in os.listdir(DIR)) if m)
    if not tamanhos:
        print('nenhum resultado em', DIR, file=sys.stderr)
        return 1
    print('# Resultados das consultas oficiais\n')
    print('Gerado por `scripts/tabelas.py` a partir de `docs/resultados/consultas_<BS>.txt`.\n')
    for bs in tamanhos:
        print(tabelas_md(bs, carregar(bs)))
    return 0


if __name__ == '__main__':
    sys.exit(main())
