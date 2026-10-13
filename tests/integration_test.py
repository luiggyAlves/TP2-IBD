#!/usr/bin/env python3
"""Teste de ponta a ponta dos 7 programas do TP2.

Uso: python3 tests/integration_test.py <arquivo.csv> [diretorio-bin]

O script:
  1. lê o CSV com um parser independente (mesmas regras do CsvReader em C++)
     e monta o "gabarito" em memória;
  2. roda o upload num diretório temporário (variável TP2_DB_DIR);
  3. roda findrec/seek1, findtitle/seek2 e findrange/seekrange sobre as
     consultas oficiais e sobre consultas sorteadas, e confere:
       - os registros devolvidos (todos os campos) contra o gabarito;
       - que a Parte 1 e a Parte 2 devolvem exatamente os mesmos registros;
       - invariantes de custo (varredura = total de blocos; seek1 = altura + 1;
         blocos de dados do seek2 = candidatos; seekrange em ordem crescente);
  4. confere mensagens e códigos de saída nos casos de erro.
"""

import os
import random
import re
import shutil
import subprocess
import sys
import tempfile
from collections import defaultdict

PREFIX = 64  # kTitlePrefixLen (include/config.hpp)


# ---------------------------------------------------------------------------
# Gabarito: parser equivalente ao src/csv_reader.cpp
# ---------------------------------------------------------------------------
def split_fields(line):
    """Mesma máquina de estados de split_fields() em csv_reader.cpp."""
    out, campo, dentro, iniciado = [], [], False, False
    i = 0
    while i < len(line):
        c = line[i]
        if dentro:
            if c == '"':
                if i + 1 < len(line) and line[i + 1] == '"':
                    campo.append('"')
                    i += 1
                else:
                    dentro = False
            else:
                campo.append(c)
        elif c == '"':
            dentro, iniciado = True, True
        elif c == ';':
            out.append(''.join(campo))
            campo, iniciado = [], False
        elif c != '\r':
            campo.append(c)
        i += 1
    if dentro:
        return None
    if iniciado or campo or out:
        out.append(''.join(campo))
    return out


def atoi(s):
    """Equivalente a std::atoi: sinal opcional + dígitos iniciais; senão 0."""
    m = re.match(r'\s*([+-]?\d+)', s)
    return int(m.group(1)) if m else 0


def load_reference(csv_path):
    recs = {}
    with open(csv_path, 'rb') as f:
        for raw in f:
            line = raw.rstrip(b'\n').decode('utf-8', 'surrogateescape')
            if not line:
                continue
            campos = split_fields(line)
            if campos is None or len(campos) < 7:
                continue
            r = {
                'ID': atoi(campos[0]), 'Titulo': campos[1], 'Ano': atoi(campos[2]),
                'Autores': campos[3], 'Citacoes': atoi(campos[4]),
                'Atualizacao': campos[5][:19], 'Snippet': campos[6],
            }
            recs[r['ID']] = r
    return recs


# ---------------------------------------------------------------------------
# Execução dos programas e leitura da saída
# ---------------------------------------------------------------------------
class Runner:
    def __init__(self, bindir, dbdir):
        self.bindir, self.env = bindir, dict(os.environ, TP2_DB_DIR=dbdir)
        self.env.pop('TP2_TRACE', None)

    def run(self, prog, *args):
        p = subprocess.run([os.path.join(self.bindir, prog), *map(str, args)], env=self.env,
                           stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        return p.returncode, p.stdout.decode('utf-8', 'surrogateescape'), p.stderr.decode('utf-8', 'surrogateescape')


def resumo(out):
    """Extrai a linha 'RESUMO prog chave=valor ...' como dicionário."""
    for line in out.split('\n'):
        if line.startswith('RESUMO '):
            return {k: int(v) for k, v in (kv.split('=') for kv in line.split()[2:])}
    raise AssertionError('saida sem linha RESUMO:\n' + out[-2000:])


FIELD_RE = re.compile(r'^  (ID|Titulo|Ano|Autores|Citacoes|Atualizacao|Snippet)\s*: ?(.*)$')


def records_full(out):
    """Registros impressos por print_record (um campo por linha)."""
    recs, cur = [], None
    for line in out.split('\n'):
        m = FIELD_RE.match(line)
        if not m:
            continue
        k, v = m.group(1), m.group(2)
        if k == 'ID':
            cur = {}
            recs.append(cur)
        cur[k] = int(v) if k in ('ID', 'Ano', 'Citacoes') else v
    return recs


def records_line_ids(out):
    """IDs dos registros impressos por print_record_line ('ID | ...')."""
    ids = []
    for line in out.split('\n'):
        m = re.match(r'^(-?\d+) \| ', line)
        if m:
            ids.append(int(m.group(1)))
    return ids


# ---------------------------------------------------------------------------
# Verificações
# ---------------------------------------------------------------------------
class Checker:
    def __init__(self):
        self.ok = self.fail = 0

    def __call__(self, cond, msg):
        if cond:
            self.ok += 1
        else:
            self.fail += 1
            print('FALHOU:', msg)


def same_record(got, ref):
    return all(got.get(k) == ref[k] for k in ref)


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    csv_path = sys.argv[1]
    bindir = sys.argv[2] if len(sys.argv) > 2 else 'bin'
    check = Checker()
    random.seed(2026)

    print('lendo gabarito de', csv_path, '...')
    ref = load_reference(csv_path)
    by_title = defaultdict(list)
    by_prefix = defaultdict(set)
    for r in ref.values():
        by_title[r['Titulo']].append(r['ID'])
        by_prefix[r['Titulo'].encode('utf-8', 'surrogateescape')[:PREFIX]].add(r['Titulo'])
    ids_sorted = sorted(ref)
    print(f'  {len(ref)} registros, {len(by_title)} titulos distintos')

    dbdir = tempfile.mkdtemp(prefix='tp2_it_')
    try:
        run = Runner(bindir, dbdir)

        # --- erros antes do upload ---------------------------------------
        rc, out, err = run.run('findrec', 5)
        check(rc == 1 and 'erro' in err, 'findrec sem arquivo de dados deve falhar com mensagem')
        rc, out, err = run.run('seek1', 5)
        check(rc == 1 and 'erro' in err, 'seek1 sem indice deve falhar com mensagem')

        # --- upload --------------------------------------------------------
        print('upload ...')
        rc, out, err = run.run('upload', csv_path)
        check(rc == 0, 'upload terminou com codigo %d: %s' % (rc, err))
        up = resumo(out)
        check(up['registros'] == len(ref), f"upload carregou {up['registros']}, gabarito tem {len(ref)}")
        total_dados = up['blocos_dados']
        for f in ('artigos.dat', 'idx_id.bpt', 'idx_titulo.bpt'):
            check(os.path.exists(os.path.join(dbdir, f)), f'arquivo {f} nao foi criado')

        # --- consultas por ID ---------------------------------------------
        ids = [5, 316639, 663752, 1014283, 1483294, 1, 2000000]
        ids += random.sample(ids_sorted, 150)
        ids += [random.randint(-10, ids_sorted[-1] + 10) for _ in range(40)]  # muitos inexistentes
        ids += [ids_sorted[0], ids_sorted[-1], -1, 0, 2**31 - 1, -2**31]
        print(f'{len(ids)} consultas por ID ...')
        for i in ids:
            rc1, o1, _ = run.run('findrec', i)
            rc2, o2, _ = run.run('seek1', i)
            check(rc1 == 0 and rc2 == 0, f'findrec/seek1 {i}: codigo de saida')
            r1, r2 = resumo(o1), resumo(o2)
            existe = i in ref
            check(r1['encontrados'] == int(existe), f'findrec {i}: encontrados={r1["encontrados"]}')
            check(r2['encontrados'] == int(existe), f'seek1 {i}: encontrados={r2["encontrados"]}')
            if existe:
                g1, g2 = records_full(o1), records_full(o2)
                check(len(g1) == 1 and same_record(g1[0], ref[i]), f'findrec {i}: registro difere do gabarito')
                check(len(g2) == 1 and same_record(g2[0], ref[i]), f'seek1 {i}: registro difere do gabarito')
            else:
                check('AVISO' in o1 and 'AVISO' in o2, f'{i}: falta mensagem de nao encontrado')
            # custos
            check(2 <= r1['blocos_lidos_dados'] <= 1 + 3, f'findrec {i}: {r1["blocos_lidos_dados"]} blocos')
            check(r1['total_blocos_dados'] == total_dados, f'findrec {i}: total de blocos')
            check(r2['blocos_lidos_indice'] == r2['altura'], f'seek1 {i}: leituras no indice != altura')
            check(r2['blocos_lidos_dados'] == int(existe), f'seek1 {i}: leituras de dados')

        # --- consultas por título -----------------------------------------
        oficiais = [
            'Design and implementation of an immersive virtual reality system based on a smartphone platform.',
            'A rotating-magnet based wireless electrical power transfer for biomedical implants',
            'Review of particle physics particle data group',
            'Two-dimensional gas of massless Dirac fermions in graphene',
            'Um titulo que nao existe na base',
        ]
        repetidos = sorted((t for t in by_title if len(by_title[t]) > 1), key=lambda t: -len(by_title[t]))[:5]
        colisoes = [t for ts in by_prefix.values() if len(ts) > 1 for t in sorted(ts)][:6]
        sorteados = random.sample(sorted(by_title), 15)
        titulos = oficiais + repetidos + colisoes + sorteados + ['', 'x' * 300]
        print(f'{len(titulos)} consultas por titulo ...')
        for t in titulos:
            rc1, o1, _ = run.run('findtitle', t)
            rc2, o2, _ = run.run('seek2', t)
            check(rc1 == 0 and rc2 == 0, f'findtitle/seek2 {t[:40]!r}: codigo de saida')
            r1, r2 = resumo(o1), resumo(o2)
            esperado = sorted(by_title.get(t, []))
            g1, g2 = records_full(o1), records_full(o2)
            check(sorted(r['ID'] for r in g1) == esperado, f'findtitle {t[:40]!r}: IDs diferem')
            check(sorted(r['ID'] for r in g2) == esperado, f'seek2 {t[:40]!r}: IDs diferem')
            check(all(same_record(r, ref[r['ID']]) for r in g1 + g2), f'{t[:40]!r}: campos diferem')
            check(r1['encontrados'] == len(esperado) and r2['encontrados'] == len(esperado),
                  f'{t[:40]!r}: contagem de encontrados')
            check(r1['blocos_lidos_dados'] == total_dados, f'findtitle {t[:40]!r}: nao leu o arquivo todo')
            check(r2['blocos_lidos_dados'] == r2['candidatos'], f'seek2 {t[:40]!r}: 1 bloco por candidato')
            pref = t.encode('utf-8', 'surrogateescape')[:PREFIX]
            cand = sum(len(by_title[x]) for x in by_prefix.get(pref, ()))
            check(r2['candidatos'] == cand, f'seek2 {t[:40]!r}: candidatos {r2["candidatos"]} != {cand}')
            check(r2['blocos_lidos_indice'] >= r2['altura'], f'seek2 {t[:40]!r}: leu menos que a altura')
            if not esperado:
                check('AVISO' in o1 and 'AVISO' in o2, f'{t[:40]!r}: falta mensagem de nao encontrado')

        # --- consultas por intervalo --------------------------------------
        faixas = [(1, 50), (1000, 1100), (500000, 500500), (200000, 260000), (1600000, 1700000)]
        for _ in range(15):
            a = random.randint(0, ids_sorted[-1])
            faixas.append((a, a + random.choice([0, 3, 40, 900, 25000])))
        faixas += [(ids_sorted[0], ids_sorted[0]), (ids_sorted[-1], ids_sorted[-1] + 100),
                   (-100, 4), (ids_sorted[-1] + 1, ids_sorted[-1] + 1)]
        print(f'{len(faixas)} consultas por intervalo ...')
        for a, b in faixas:
            rc1, o1, _ = run.run('findrange', a, b)
            rc2, o2, _ = run.run('seekrange', a, b)
            check(rc1 == 0 and rc2 == 0, f'findrange/seekrange {a} {b}: codigo de saida')
            r1, r2 = resumo(o1), resumo(o2)
            esperado = [i for i in ids_sorted if a <= i <= b]
            got1, got2 = records_line_ids(o1), records_line_ids(o2)
            check(got1 == esperado, f'findrange {a} {b}: {len(got1)} IDs, esperado {len(esperado)}')
            check(got2 == esperado, f'seekrange {a} {b}: {len(got2)} IDs (ou fora de ordem), esperado {len(esperado)}')
            check(r1['blocos_lidos_dados'] == total_dados, f'findrange {a} {b}: nao leu o arquivo todo')
            check(r2['blocos_lidos_dados'] == len(esperado), f'seekrange {a} {b}: 1 bloco de dados por registro')
            check(r2['blocos_lidos_indice'] >= r2['altura'], f'seekrange {a} {b}: leu menos que a altura')
            if not esperado:
                check('AVISO' in o1 and 'AVISO' in o2, f'{a} {b}: falta mensagem de nao encontrado')

        # --- argumentos inválidos -----------------------------------------
        for prog, args in [('findrec', ['abc']), ('seek1', ['12x']), ('findrange', ['10', '5']),
                           ('seekrange', ['a', '5']), ('findrec', []), ('seek2', []), ('findrange', ['1']),
                           ('findrec', ['9999999999']), ('seek1', ['-9999999999']),
                           ('findrange', ['1', '9999999999']), ('seekrange', ['-9999999999', '5'])]:
            rc, _, err = run.run(prog, *args)
            check(rc == 2 and err.strip() != '', f'{prog} {args}: esperado codigo 2 com mensagem, veio {rc}')
    finally:
        shutil.rmtree(dbdir, ignore_errors=True)

    print(f'integracao: {check.ok} verificacoes ok, {check.fail} falhas')
    return 0 if check.fail == 0 else 1


if __name__ == '__main__':
    sys.exit(main())
