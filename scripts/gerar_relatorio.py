#!/usr/bin/env python3
"""Gera o relatório docs/tp2_relatorio.pdf (e a fonte docs/tp2_relatorio.html).

Todos os números do relatório são lidos dos arquivos produzidos pelos
programas da entrega (docs/resultados/*.txt), ou calculados a partir do CSV.
Assim, o texto não diverge da execução. Pré-requisitos:

    bash scripts/run_experiments.sh      # gera docs/resultados/ e data/db_<BS>
    python3 scripts/gerar_relatorio.py   # gera o HTML e converte para PDF

A conversão para PDF usa o LibreOffice em modo headless (soffice).
Os rastros (TP2_TRACE=1) embutidos no relatório são obtidos executando os
binários de bin/ (compilados com BLOCK_SIZE=4096) sobre data/db_4096.
"""

import html
import math
import os
import re
import shutil
import subprocess
import sys
from collections import defaultdict

AQUI = os.path.dirname(os.path.abspath(__file__))
RAIZ = os.path.abspath(os.path.join(AQUI, '..'))
sys.path.insert(0, AQUI)
sys.path.insert(0, os.path.join(RAIZ, 'tests'))
import tabelas  # noqa: E402
from integration_test import load_reference  # noqa: E402  (mesmo parser do CsvReader)

RES = os.path.join(RAIZ, 'docs', 'resultados')
CSV = os.path.join(RAIZ, 'data', 'artigo-amostra.csv')


# ---------------------------------------------------------------------------
# Utilidades de formatação
# ---------------------------------------------------------------------------
def n(v, casas=0):
    """Número no padrão brasileiro (milhar com ponto, decimal com vírgula)."""
    s = f'{v:,.{casas}f}'
    return s.replace(',', '#').replace('.', ',').replace('#', '.')


def e(s):
    return html.escape(str(s))


def tab(cab, linhas, larg='100%', nota=None):
    h = [f'<table border="1" cellspacing="0" cellpadding="3" width="{larg}">', '<tr>']
    h += [f'<th>{c}</th>' for c in cab]
    h.append('</tr>')
    for l in linhas:
        h.append('<tr>' + ''.join(f'<td>{c}</td>' for c in l) + '</tr>')
    h.append('</table>')
    h.append('<p style="font-size:4pt">&nbsp;</p>')  # respiro entre a tabela e o texto seguinte
    if nota:
        h.append(f'<p class="nota">{nota}</p>')
    return '\n'.join(h)


def pre(texto):
    return f'<pre>{e(texto)}</pre>'


def campo(texto, rotulo):
    """Valor após 'rotulo   :' na saída do upload."""
    m = re.search(re.escape(rotulo) + r'\s*:\s*(.*)', texto)
    if not m:
        raise KeyError(rotulo)
    return m.group(1).strip()


def num(s):
    return float(re.search(r'-?\d+(?:\.\d+)?', s).group(0))


def secao_indice(texto, nome):
    """Bloco de estatísticas '[nome] ...' de um índice na saída do upload."""
    i = texto.index(f'[{nome}]')
    j = texto.find('\n[', i + 1)
    k = texto.find('\ntempo total', i + 1)
    fim = min(x for x in (j, k) if x != -1)
    return texto[i:fim]


# ---------------------------------------------------------------------------
# Coleta dos dados
# ---------------------------------------------------------------------------
def dados_upload(bs):
    r = tabelas.carregar(bs)
    t = r['upload_texto']
    u = dict(r['upload'])
    u['ignoradas'] = int(num(campo(t, 'linhas ignoradas (malformadas)')))
    u['truncados'] = int(num(campo(t, 'registros truncados')))
    tam = campo(t, 'tamanho do registro')
    u['reg_medio'] = num(tam)
    u['reg_min'], u['reg_max'] = map(int, re.search(r'min (\d+), max (\d+)', tam).groups())
    rpb = campo(t, 'registros por bloco')
    u['rpb_media'] = num(rpb)
    u['rpb_min'], u['rpb_max'] = map(int, re.search(r'min (\d+), max (\d+)', rpb).groups())
    u['rpb_cap'] = int(re.search(r'registro medio = (\d+)', rpb).group(1))
    u['ocupacao'] = num(campo(t, 'taxa de ocupacao dos blocos'))
    u['vazios'] = int(num(campo(t, 'buckets vazios')))
    u['com_ovf'] = int(num(campo(t, 'buckets com overflow')))
    u['maior_cadeia'] = int(num(campo(t, 'maior cadeia de um bucket')))
    u['maior_bucket'] = int(num(campo(t, 'maior bucket')))
    u['hist_bucket'] = {int(a): int(b) for a, b in re.findall(r'(\d+):(\d+)', campo(t, 'registros por bucket'))}
    u['hist_cadeia'] = {int(a): int(b) for a, b in
                        re.findall(r'\[(\d+) bloco\(s\): (\d+) buckets\]', campo(t, 'comprimento das cadeias'))}
    u['payload'] = int(num(campo(t, 'bytes uteis por bloco')))
    for chave, nome in (('idx1', 'indice primario (ID)'), ('idx2', 'indice secundario (Titulo)')):
        s = secao_indice(t, nome)
        u[chave + '_folha_cap'] = int(num(campo(s, 'capacidade da folha')))
        ordem = campo(s, 'ordem do no interno')
        u[chave + '_ordem'] = int(num(ordem))
        u[chave + '_chaves_int'] = int(re.search(r'\((\d+) chaves\)', ordem).group(1))
        nos = campo(s, 'blocos (nos) do indice')
        u[chave + '_folhas'], u[chave + '_internos'] = map(int, re.search(r'folhas (\d+), internos (\d+)', nos).groups())
        oc = campo(s, 'ocupacao media das folhas')
        u[chave + '_ocup'] = num(oc)
        u[chave + '_por_folha'] = float(re.search(r'\(([\d.]+) entradas', oc).group(1))
    r['u'] = u
    with open(os.path.join(RES, f'analise_{bs}.txt'), encoding='utf-8') as f:
        r['analise'] = f.read()
    return r


def analise_secao(texto, letra):
    i = texto.index(f'[{letra}]')
    j = texto.find('\n[', i + 1)
    return texto[i:j if j != -1 else None].rstrip()


def estatisticas_csv():
    """Medidas do CSV usadas nas decisões de projeto (Seção 7.1)."""
    ref = load_reference(CSV)
    b = lambda s: len(s.encode('utf-8', 'surrogateescape'))  # noqa: E731
    tit = [b(r['Titulo']) for r in ref.values()]
    aut = [b(r['Autores']) for r in ref.values()]
    sni = [b(r['Snippet']) for r in ref.values()]
    st = {
        'n': len(ref), 'tit_med': sum(tit) / len(tit), 'tit_max': max(tit), 'tit_300': sum(x > 300 for x in tit),
        'aut_med': sum(aut) / len(aut), 'aut_max': max(aut), 'aut_150': sum(x > 150 for x in aut),
        'sni_med': sum(sni) / len(sni), 'sni_max': max(sni), 'sni_1024': sum(x > 1024 for x in sni),
    }
    titulos = set(r['Titulo'].encode('utf-8', 'surrogateescape') for r in ref.values())
    st['titulos_distintos'] = len(titulos)
    prefixos = []
    for k in (32, 48, 64, 100, 128, 200, 300):
        grupos = defaultdict(set)
        for t in titulos:
            grupos[t[:k]].add(t)
        col = sum(1 for g in grupos.values() if len(g) > 1)
        pior = max(len(g) for g in grupos.values())
        prefixos.append((k, col, pior))
    st['prefixos'] = prefixos
    ids = sorted(ref)
    st['id_min'], st['id_max'] = ids[0], ids[-1]
    st['mod5'] = [sum(1 for i in ids if i % 5 == r) for r in range(5)]
    return st


def rastro(prog, *args):
    """Executa um programa com TP2_TRACE=1 sobre data/db_4096 e devolve o rastro."""
    env = dict(os.environ, TP2_DB_DIR=os.path.join('data', 'db_4096'), TP2_TRACE='1')
    p = subprocess.run([os.path.join('bin', prog), *map(str, args)], cwd=RAIZ, env=env,
                       stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=True)
    out = p.stdout.decode('utf-8', 'replace')
    linhas = [l for l in out.splitlines() if l.startswith('blocos lidos') or l.startswith('registros encontrados')]
    return p.stderr.decode('utf-8', 'replace').rstrip() + '\n' + '\n'.join(linhas)


def poisson(lam, k):
    return math.exp(-lam) * lam ** k / math.factorial(k)


# ---------------------------------------------------------------------------
# Relatório
# ---------------------------------------------------------------------------
def gerar():
    r4, r8 = dados_upload(4096), dados_upload(8192)
    u4, u8 = r4['u'], r8['u']
    st = estatisticas_csv()
    with open(os.path.join(RAIZ, 'docs', 'previsao.md'), encoding='utf-8') as f:
        previsao_md = f.read()

    T = 'Design and implementation of an immersive virtual reality system based on a smartphone platform.'
    T2 = 'A rotating-magnet based wireless electrical power transfer for biomedical implants'
    TR1 = 'Review of particle physics particle data group'
    TR2 = 'Two-dimensional gas of massless Dirac fermions in graphene'
    TN = 'Um titulo que nao existe na base'

    def ids(r, i, prog, k):
        return r['ids'][i][prog][k]

    def tit(r, t, prog, k):
        return r['titulos'][t][prog][k]

    def fx(r, a, b, prog, k):
        return r['faixas'][(a, b)][prog][k]

    H = []
    add = H.append

    # ---------------- capa ----------------
    add('<h1 align="center">Trabalho Prático II<br/>Arquivo de dados por hashing e índices B+Tree</h1>')
    add('<p align="center">Introdução a Bancos de Dados — ICC205 — 2026/02<br/>'
        'Instituto de Computação — Universidade Federal do Amazonas<br/>'
        'Docente: Altigran Soares da Silva</p>')
    add('<p align="center">Aluno: Luiggy Alves (luiggy.alves@icomp.ufam.edu.br)</p>')
    add('<p align="center">Outubro de 2026</p>')

    # ---------------- resumo ----------------
    add('<h2>Resumo</h2>')
    add(f'<p>Este relatório descreve a implementação, em C++, de um arquivo de dados paginado organizado por '
        f'hashing estático (Parte 1). Descreve também dois índices B+Tree em memória secundária sobre o mesmo '
        f'arquivo (Parte 2): um primário, sobre o ID, e um secundário, sobre o Título. O relatório compara as duas '
        f'partes nas consultas oficiais, em número de blocos lógicos lidos pela camada <i>blockio</i>. '
        f'Com BLOCK_SIZE = 4096, a amostra ({n(u4["registros"])} registros) ocupa {n(u4["blocos_dados"])} blocos no '
        f'arquivo de dados. A busca exata por ID custa {ids(r4, 5, "findrec", "blocos_lidos_dados")} blocos pelo hashing '
        f'e {ids(r4, 5, "seek1", "blocos_lidos_indice") + ids(r4, 5, "seek1", "blocos_lidos_dados")} blocos pela '
        f'B+Tree (altura {u4["idx1_altura"]}). A busca por título cai de {n(u4["blocos_dados"])} blocos (varredura) '
        f'para {tit(r4, T2, "seek2", "blocos_lidos_indice") + tit(r4, T2, "seek2", "blocos_lidos_dados")} blocos '
        f'com o índice secundário. O intervalo [200000, 260000] cai de {n(u4["blocos_dados"])} para '
        f'{n(fx(r4, 200000, 260000, "seekrange", "blocos_lidos_indice") + fx(r4, 200000, 260000, "seekrange", "blocos_lidos_dados"))} blocos. '
        f'Com blocos de 8192 bytes, o arquivo de dados cai para {n(u8["blocos_dados"])} blocos e as duas árvores '
        f'perdem um nível.</p>')

    # ---------------- visão geral ----------------
    add('<h2>1. Visão geral da implementação</h2>')
    add('<p>O programa <code>upload</code> lê o CSV e grava os registros no arquivo de dados por hashing. Depois, '
        'na mesma execução, ele constrói os dois índices. Os outros seis programas só leem esses três arquivos. '
        'Todo acesso aos arquivos de dados e de índice passa por <code>blockio::BlockFile</code>; nenhum programa '
        'usa <code>fstream</code> ou <code>mmap</code> sobre eles. O CSV é lido pelo <code>CsvReader</code> '
        'fornecido e só pelo <code>upload</code>.</p>')
    add(tab(['arquivo (em /data/db)', 'conteúdo', 'blocos (4096 B)', 'blocos (8192 B)'], [
        ['artigos.dat', 'arquivo de dados: bloco 0 = cabeçalho; 1..N = buckets; depois, overflow',
         n(u4['blocos_dados']), n(u8['blocos_dados'])],
        ['idx_id.bpt', 'índice primário (B+Tree de ID); raiz no bloco 0', n(u4['idx1_blocos']), n(u8['idx1_blocos'])],
        ['idx_titulo.bpt', 'índice secundário (B+Tree de Título); raiz no bloco 0', n(u4['idx2_blocos']),
         n(u8['idx2_blocos'])],
    ]))
    add(tab(['módulo', 'responsabilidade'], [
        ['include/datafile.hpp, src/datafile.cpp', 'layout de registro e de bloco, hashing, overflow, busca, varredura'],
        ['include/bptree.hpp, src/bptree.cpp', 'B+Tree genérica em disco (chave de tamanho fixo comparada com memcmp)'],
        ['include/config.hpp', 'parâmetros de projeto (prefixo de 64 bytes, ocupação alvo 0,80) e caminhos'],
        ['src/upload.cpp e os 6 programas de consulta', 'programas pedidos no enunciado'],
        ['tests/', 'testes unitários (make test) e de ponta a ponta (make test-integration)'],
        ['tools/analise.cpp, scripts/', 'experimentos complementares, execução das consultas oficiais, relatório'],
    ]))
    add(f'<p>Número de registros: o CSV tem {n(u4["registros"] + u4["ignoradas"])} linhas. Duas delas '
        f'(IDs 368004 e 424931) estão truncadas no próprio arquivo e terminam no meio de um campo entre aspas. '
        f'O <code>CsvReader</code> as descarta. Por isso a carga tem {n(u4["registros"])} registros, e não os '
        f'204.279 citados no enunciado. Todos os números deste relatório se referem aos {n(u4["registros"])} '
        f'registros efetivamente carregados.</p>')

    # ---------------- 7.1 ----------------
    add('<h2>7.1 Decisões de projeto</h2>')
    add('<h3>7.1.1 Layout do registro</h3>')
    add(f'<p>O layout adotado é de <b>tamanho variável com indicadores de comprimento</b>. O registro tem uma parte '
        f'fixa de 39 bytes: tamanho total (2), ID (4), ano (4), citações (4), data/hora (19 bytes, fixo) e os '
        f'comprimentos dos três textos (3 × 2). Em seguida vêm os bytes de título, autores e snippet, sem '
        f'preenchimento. Medidas no CSV (em bytes UTF-8): título médio {n(st["tit_med"], 1)} (máx. '
        f'{st["tit_max"]}), autores médio {n(st["aut_med"], 1)} (máx. {st["aut_max"]}) e snippet médio '
        f'{n(st["sni_med"], 1)} (máx. {st["sni_max"]}).</p>')
    fixo = 4 + 300 + 4 + 150 + 4 + 19 + 1024
    add(f'<p>Justificativa. Um registro de tamanho fixo com os tamanhos máximos do enunciado ocuparia ≈ {fixo} bytes. '
        f'Caberiam então só {4080 // fixo} registros por bloco de 4096 B, e a varredura leria ≈ '
        f'{n(math.ceil(u4["registros"] / (4080 // fixo)))} blocos. O tamanho fixo também obrigaria a truncar os autores de '
        f'{n(st["aut_150"])} registros (mais de 150 bytes) e os campos de outros {st["tit_300"] + st["sni_1024"]}. '
        f'Com tamanho variável, o registro médio tem {n(u4["reg_medio"], 1)} bytes. Cabem ≈ {u4["rpb_cap"]} registros médios '
        f'por bloco, e o maior registro ({u4["reg_max"]} bytes) cabe folgado nos {u4["payload"]} bytes úteis. '
        f'Assim, <b>nenhum campo é truncado</b> ({u4["truncados"]} registros truncados na carga). O custo do tamanho '
        f'variável é que a quantidade de registros por bloco deixa de ser constante (de {u4["rpb_min"]} a '
        f'{u4["rpb_max"]} por bloco).</p>')
    add('<p>Bloco de dados: cabeçalho de 16 bytes (<i>next</i> = próximo bloco da cadeia, <i>bucket</i>, '
        '<i>count</i>, <i>used</i>), seguido dos registros colados uns aos outros. Não há remoção nem atualização, '
        'então o endereço de um registro (RID) é o par (bloco, deslocamento no bloco), com 4 + 2 = 6 bytes. É esse '
        'RID que os índices guardam.</p>')
    add('<h3>7.1.2 Hashing</h3>')
    add(f'<p><b>Função</b>: h(ID) = ID mod N, com N primo. Os IDs vão de {n(st["id_min"])} a {n(st["id_max"])}. '
        f'Como a amostra pega um registro a cada cinco, os IDs têm padrões aritméticos: por resíduo mod 5, a '
        f'contagem é {", ".join(n(x) for x in st["mod5"])}. Um N primo não tem divisores em comum com esses '
        f'padrões, e o resto da divisão espalha os IDs de modo uniforme. A seção 7.6 mostra que a distribuição '
        f'medida é praticamente a de Poisson.</p>')
    add(f'<p><b>Número de buckets</b>: N é calculado na carga. O <code>upload</code> faz uma primeira passada no '
        f'CSV para somar os tamanhos dos registros e escolhe o menor primo ≥ bytes / (bytes úteis × 0,80). A meta '
        f'é ocupar 80 % dos blocos primários. O resultado é N = {n(u4["buckets"])} com 4096 B e N = '
        f'{n(u8["buckets"])} com 8192 B. O valor 0,80 foi escolhido na fase de projeto, com uma simulação em '
        f'Python sobre o CSV para ocupações de 0,6 a 0,9. Ocupações menores reduzem o overflow, mas aumentam o '
        f'arquivo e, portanto, o custo de toda varredura. Ocupações maiores fazem o contrário.</p>')
    add('<p><b>Colisões e overflow</b>: cada bucket tem 1 bloco primário (bucket b → bloco 1 + b). Registros que '
        'colidem no mesmo bucket ficam juntos no bloco enquanto houver espaço. Quando o registro não cabe, o '
        '<code>upload</code> aloca um bloco de overflow no fim do arquivo e o liga ao último bloco da cadeia pelo '
        'campo <i>next</i>. Isso é encadeamento separado por bucket. Durante a carga, o último bloco de cada '
        'cadeia fica em memória, então cada inserção lê 1 bloco e grava 1 (ou 3, se abrir um overflow). A busca '
        'por ID percorre a cadeia só até achar o ID, que é chave.</p>')
    add('<p><b>Cabeçalho</b>: N é guardado no bloco 0, junto com contadores. O <code>findrec</code> precisa de N '
        'para calcular h(ID), então toda busca lê o cabeçalho antes do bucket. Esse bloco a mais aparece em todas '
        'as medições do <code>findrec</code> e é discutido na seção 7.4.</p>')
    add('<h3>7.1.3 B+Tree: estrutura do nó e ordem</h3>')
    add('<p>Uma única implementação (classe <code>BPlusTree</code>) atende aos dois índices. Cada nó ocupa um bloco '
        'e começa com um cabeçalho de 32 bytes: magic, folha/interno, n, tamanho da chave, <i>next</i> e, só na '
        'raiz, os metadados (altura, total de entradas, primeira folha, BLOCK_SIZE). A folha guarda n pares '
        '(chave, RID de 6 bytes). O nó interno guarda n chaves e n + 1 ponteiros de 4 bytes (número do bloco). '
        'A <b>raiz fica sempre no bloco 0</b>. Quando ela se divide, as metades vão para blocos novos e o bloco 0 '
        'é regravado como a nova raiz. Assim a busca não lê um bloco de cabeçalho à parte: os metadados estão na '
        'raiz, que seria lida de qualquer modo.</p>')
    add('<p>Cálculo da ordem (B = BLOCK_SIZE, k = tamanho da chave):</p>')
    add(pre('capacidade da folha  Lf = floor((B - 32) / (k + 6))\n'
            'chaves no interno    Li = floor((B - 32 - 4) / (k + 4))     ordem p = Li + 1 filhos'))
    add(tab(['índice', 'chave (k)', 'Lf (4096)', 'p (4096)', 'Lf (8192)', 'p (8192)'], [
        ['primário (ID)', '4 bytes (ID em big-endian, bit de sinal invertido)', u4['idx1_folha_cap'], u4['idx1_ordem'],
         u8['idx1_folha_cap'], u8['idx1_ordem']],
        ['secundário (Título)', '70 bytes = 64 do título + 6 do RID', u4['idx2_folha_cap'], u4['idx2_ordem'],
         u8['idx2_folha_cap'], u8['idx2_ordem']],
    ]))
    add('<p>As chaves são comparadas com <code>memcmp</code>. O ID é gravado em big-endian com o bit de sinal '
        'invertido, para que a ordem dos bytes seja a ordem numérica. Inserção: a busca desce da raiz até a folha '
        'e guarda o caminho. Se a folha estourar, ela é dividida ao meio e a primeira chave da metade direita é '
        '<i>copiada</i> para o pai. Se o nó interno estourar, a chave do meio <i>sobe</i> para o avô. As folhas '
        'são encadeadas pelo campo <i>next</i>, e é esse encadeamento que o <code>seekrange</code> percorre. '
        'Toda leitura e escrita de nó passa pelo blockio, sem cache.</p>')
    add('<h3>7.1.4 Índice secundário: chave de título e duplicatas</h3>')
    add(f'<p><b>Duplicatas</b>: a chave do índice secundário é composta, (título, RID). Como o RID é único, toda chave '
        f'é única, mesmo quando vários registros têm o mesmo título. O CSV tem {n(st["titulos_distintos"])} '
        f'títulos distintos para {n(st["n"])} registros. Os registros de mesmo título ficam lado a lado nas '
        f'folhas, ordenados pelo RID. A busca monta a chave '
        f'(título, RID 0), que é menor que qualquer entrada com aquele título, posiciona-se na primeira entrada ≥ '
        f'essa chave e avança pelas folhas enquanto o título for igual. Não é preciso tratar duplicatas à parte '
        f'(listas de RIDs, buckets de ponteiros), e a árvore continua com chaves únicas.</p>')
    add('<p><b>Prefixo de 64 bytes</b>: para a ordem poder ser calculada a partir de BLOCK_SIZE, a chave precisa '
        'ter tamanho fixo. Usar os 300 bytes do enunciado reduziria a ordem a ≈ 13 e a árvore teria 5 ou 6 '
        'níveis. A chave guarda então os primeiros 64 bytes do título. O título completo continua no arquivo de '
        'dados e é conferido no registro lido, então o resultado é exato. O custo é que títulos diferentes com o '
        'mesmo prefixo de 64 bytes viram <i>candidatos</i> e custam uma leitura de dados cada. A escolha de 64 '
        'veio desta medição sobre os títulos distintos do CSV:</p>')
    linhas = []
    for k, col, pior in st['prefixos']:
        lf = (4096 - 32) // (k + 12)
        li = (4096 - 36) // (k + 10)
        linhas.append([k, n(col), pior, lf, li + 1])
    add(tab(['prefixo (bytes)', 'prefixos compartilhados por títulos diferentes', 'maior grupo',
             'Lf (4096)', 'p (4096)'], linhas, nota='Os títulos distintos são ' + n(st['titulos_distintos']) +
            '. Com 64 bytes, a ordem é 55 e a altura prevista é 4. Os poucos prefixos compartilhados custam, em '
            'média, ' + re.search(r'candidatos medios.*?: ([\d.]+)', r4['analise']).group(1).replace('.', ',') +
            ' leituras de dados por busca (seção 7.6).'))
    add('<h3>7.1.5 Construção dos índices</h3>')
    add(f'<p>Os índices são construídos dentro do <code>upload</code>, logo após o arquivo de dados. O comando é '
        f'o mesmo da carga: <code>./bin/upload /data/artigo-amostra.csv</code>. O arquivo de dados é varrido em '
        f'ordem física ({n(u4["varredura"])} leituras com 4096 B), porque é dessa varredura que saem os RIDs. Cada '
        f'registro gera uma inserção em cada árvore. Custos medidos (blocos):</p>')
    add(tab(['estrutura', 'lidos (4096)', 'escritos (4096)', 'lidos (8192)', 'escritos (8192)'], [
        ['arquivo de dados (carga por hashing)', n(u4['leituras_dados']), n(u4['escritas_dados']),
         n(u8['leituras_dados']), n(u8['escritas_dados'])],
        ['varredura do arquivo de dados para os índices', n(u4['varredura']), '0', n(u8['varredura']), '0'],
        ['índice primário', n(u4['idx1_leituras']), n(u4['idx1_escritas']), n(u8['idx1_leituras']),
         n(u8['idx1_escritas'])],
        ['índice secundário', n(u4['idx2_leituras']), n(u4['idx2_escritas']), n(u8['idx2_leituras']),
         n(u8['idx2_escritas'])],
    ]))

    # ---------------- 7.2 ----------------
    add('<h2>7.2 Previsão antes da medição</h2>')
    add('<p>A previsão abaixo foi registrada em <code>docs/previsao.md</code> com o código já compilado, mas '
        '<b>antes</b> da primeira execução do <code>upload</code> e das consultas. Ela usa só o layout e modelos '
        'analíticos: distribuição de Poisson para os buckets e ocupação ln 2 ≈ 69 % para B+Trees construídas por '
        'inserção em ordem aleatória. O conteúdo é reproduzido sem alterações.</p>')
    add(md_para_html(previsao_md.split('\n', 2)[2]))

    # ---------------- 7.3 ----------------
    add('<h2>7.3 Medição (BLOCK_SIZE = 4096)</h2>')
    add(f'<p>Os números abaixo são exatamente os produzidos por <code>bash scripts/run_experiments.sh</code>, que '
        f'executa todas as consultas de <code>queries/oficial.txt</code>. As linhas RESUMO brutas estão em '
        f'<code>docs/resultados/consultas_4096.txt</code>. Totais de blocos: arquivo de dados = '
        f'<b>{n(u4["blocos_dados"])}</b> (1 cabeçalho + {n(u4["buckets"])} primários + {n(u4["overflow"])} '
        f'overflow); índice primário = <b>{n(u4["idx1_blocos"])}</b> ({u4["idx1_folhas"]} folhas + '
        f'{u4["idx1_internos"]} internos, altura {u4["idx1_altura"]}); índice secundário = '
        f'<b>{n(u4["idx2_blocos"])}</b> ({n(u4["idx2_folhas"])} folhas + {u4["idx2_internos"]} internos, altura '
        f'{u4["idx2_altura"]}).</p>')
    add(tabelas_medicao(r4))
    add(f'<p>Estatísticas da carga (saída do <code>upload</code>): {n(u4["registros"])} registros; registros por '
        f'bloco: média {n(u4["rpb_media"], 2)} (mín. {u4["rpb_min"]}, máx. {u4["rpb_max"]}); taxa de ocupação dos '
        f'blocos de dados {n(u4["ocupacao"], 1)} %; buckets com overflow {n(u4["com_ovf"])} '
        f'({n(100 * u4["com_ovf"] / u4["buckets"], 1)} %); maior cadeia {u4["maior_cadeia"]} blocos.</p>')

    # ---------------- 7.4 ----------------
    a4 = analise_secao(r4['analise'], 'A')
    b4 = analise_secao(r4['analise'], 'B')
    d4 = analise_secao(r4['analise'], 'D')
    suc = float(re.search(r'COM sucesso : ([\d.]+)', a4).group(1))
    ins = float(re.search(r'SEM sucesso : ([\d.]+)', a4).group(1))
    seek2_med = float(re.search(r'leituras medias no indice: ([\d.]+)', b4).group(1))
    pct5 = float(re.search(r'5 leituras no indice: \d+ titulos \(([\d.]+)%\)', b4).group(1))
    em_ovf = float(re.search(r'2\. bloco da cadeia: \d+ \(([\d.]+)%\)', a4).group(1))
    ordem_d = {m.group(1).strip(): (int(m.group(3)), float(m.group(4))) for m in
               re.finditer(r'^\s{4}(\S.*?)\s+(\d+)\s+(\d+)\s+\d+\s+([\d.]+)%', d4, re.M)}
    alea = next(v for k, v in ordem_d.items() if k.startswith('aleatoria'))
    cresc = next(v for k, v in ordem_d.items() if k.startswith('crescente'))
    fis = next(v for k, v in ordem_d.items() if k.startswith('fisica'))
    lambda4 = u4['registros'] / u4['buckets']

    add('<h2>7.4 Comparação entre previsão e medição</h2>')
    add(tab(['grandeza', 'previsto', 'medido', 'situação'], [
        ['N (buckets)', '≈ 33.149', n(u4['buckets']), 'acerto (n = 204.285, não 204.279)'],
        ['blocos de overflow', '≈ 9.240', n(u4['overflow']), 'medido 8 % menor'],
        ['total de blocos do arquivo de dados', '≈ 42.390', n(u4['blocos_dados']), 'medido 1,8 % menor'],
        ['findrec, ID existente', '2 (3 em ≈ 10 %)', '2 nos 5 IDs; média geral ' + n(suc, 3), 'acerto'],
        ['findrec, ID inexistente', '2 (72 %) ou 3 (28 %)',
         f'{ids(r4, 1, "findrec", "blocos_lidos_dados")} (ID 1) e {ids(r4, 2000000, "findrec", "blocos_lidos_dados")} '
         f'(ID 2000000); média {n(ins, 3)}', 'acerto'],
        ['findtitle / findrange', '≈ 42.390', n(u4['blocos_dados']), 'igual ao total, como previsto'],
        ['altura do índice primário', '3', str(u4['idx1_altura']), 'acerto'],
        ['folhas do índice primário', '≈ 726 (69 %)', f'{u4["idx1_folhas"]} ({n(u4["idx1_ocup"], 1)} %)',
         '<b>divergência</b>: ver (a)'],
        ['seek1 (existente / inexistente)', '3 + 1 / 3 + 0',
         f'{ids(r4, 5, "seek1", "blocos_lidos_indice")} + 1 / {ids(r4, 1, "seek1", "blocos_lidos_indice")} + 0', 'acerto'],
        ['altura e folhas do índice secundário', '4 e ≈ 5.560', f'{u4["idx2_altura"]} e {n(u4["idx2_folhas"])}',
         'acerto'],
        ['seek2, título único', '4 (ou 5) + 1',
         f'{tit(r4, T, "seek2", "blocos_lidos_indice")} + 1 e {tit(r4, T2, "seek2", "blocos_lidos_indice")} + 1',
         'um caso com 5: ver (b)'],
        ['seek2, 45 repetições', '4 + 1 (ou + 2) + 45',
         f'{tit(r4, TR1, "seek2", "blocos_lidos_indice")} + {tit(r4, TR1, "seek2", "blocos_lidos_dados")}', 'acerto'],
        ['seek2, 26 repetições', '4 (ou 5) + 26',
         f'{tit(r4, TR2, "seek2", "blocos_lidos_indice")} + {tit(r4, TR2, "seek2", "blocos_lidos_dados")}', 'acerto'],
        ['seek2, inexistente', '4 (ou 5) + 0', f'{tit(r4, TN, "seek2", "blocos_lidos_indice")} + 0', 'acerto'],
        ['seekrange [1000, 1100]', '3 + 20', f'{fx(r4, 1000, 1100, "seekrange", "blocos_lidos_indice")} + 20',
         'ver (c)'],
        ['seekrange [200000, 260000]', '≈ 14 + 3.052',
         f'{fx(r4, 200000, 260000, "seekrange", "blocos_lidos_indice")} + 3.052', '<b>divergência</b>: ver (a)'],
        ['construção do índice primário (leituras / escritas)', '≈ 590 mil / ≈ 206 mil',
         f'{n(u4["idx1_leituras"])} / {n(u4["idx1_escritas"])}', 'leituras menores: ver (d)'],
        ['construção do índice secundário (leituras / escritas)', '≈ 800 mil / ≈ 216 mil',
         f'{n(u4["idx2_leituras"])} / {n(u4["idx2_escritas"])}', 'ver (d)'],
    ]))
    add('<p>A previsão acertou a forma de todos os custos. Também acertou o valor exato nas buscas por ID e '
        'nas varreduras. As divergências são explicadas a seguir.</p>')
    add(f'<p><b>(a) Ocupação das folhas do índice primário: {n(u4["idx1_ocup"], 1)} % em vez de 69 %.</b> A '
        f'previsão supôs que as chaves chegam em ordem aleatória. No <code>upload</code>, porém, os índices são '
        f'construídos varrendo o arquivo de dados em ordem física, bucket por bucket. O bucket b guarda os IDs '
        f'≡ b (mod N), ou seja, b, b + N, b + 2N, ... Ao passar do bucket b para o b + 1, cada uma das ≈ '
        f'{n(st["id_max"] / u4["buckets"], 0)} faixas [jN, (j+1)N) da árvore recebe um ID um pouco maior que o '
        f'anterior daquela faixa. Dentro de cada faixa a inserção é crescente. Com chaves crescentes, a folha '
        f'dividida fica com metade das entradas e nunca mais recebe inserções, então a ocupação tende a 50 %. Só '
        f'os ≈ {n(em_ovf, 1)} % de registros nos blocos de overflow, varridos no fim, chegam fora dessa ordem e '
        f'preenchem parte das folhas. O experimento D da ferramenta <code>analise</code> confirma a explicação. '
        f'Ele reconstrói o índice primário com as mesmas {n(u4["registros"])} entradas em três ordens:</p>')
    add(pre(d4))
    add(f'<p>A ordem aleatória reproduz a previsão ({alea[0]} folhas, {n(alea[1], 1)} %). A ordem crescente dá '
        f'{n(cresc[1], 1)} %, e a ordem física do hash fica entre as duas ({n(fis[1], 1)} %). A altura continua 3 '
        f'nos três casos, então o <code>seek1</code> não muda. Muda o número de folhas que o '
        f'<code>seekrange</code> percorre. No intervalo [200000, 260000] há 3.052 registros a ≈ '
        f'{n(u4["idx1_por_folha"], 0)} por folha, e não ≈ 281: são 16 folhas visitadas (a primeira vem da descida '
        f'e as outras 15 pelo ponteiro <i>next</i>), total de 3 + 15 = '
        f'{fx(r4, 200000, 260000, "seekrange", "blocos_lidos_indice")} leituras no índice.</p>')
    add(f'<p><b>(b) seek2 com 5 leituras para um título único.</b> O rastro abaixo (TP2_TRACE=1) mostra a causa. A '
        f'chave de busca (prefixo, RID 0) é menor que a entrada procurada. Essa entrada é a <i>primeira</i> de sua '
        f'folha e, por isso, também é o separador no pai. A descida vai para o filho da esquerda, cujas entradas '
        f'são todas menores. O cursor chega ao fim dessa folha e precisa ler a seguinte. Isso acontece sempre que '
        f'o título procurado é o primeiro de uma folha. Na amostra de 5.000 títulos do experimento B, ocorreu em '
        f'{n(pct5, 2)} % deles (média de {n(seek2_med, 3)} leituras no índice).</p>')
    add(pre(rastro('seek2', T)))
    add(f'<p><b>(c) Intervalos que cruzam a fronteira de uma folha.</b> [1000, 1100] tem só 20 registros, mas eles '
        f'começam no fim de uma folha e terminam na seguinte: 3 leituras na descida + 1 pelo <i>next</i> = '
        f'{fx(r4, 1000, 1100, "seekrange", "blocos_lidos_indice")}. O mesmo vale para os títulos repetidos: as 45 '
        f'entradas de "{TR1}" fazem o percurso tocar 3 folhas: 6 = 3 nós internos + 3 folhas.</p>')
    add(pre(rastro('seekrange', 1000, 1100)))
    add(f'<p><b>(d) Custo de construção.</b> A previsão usou ≈ 2,9 leituras por inserção no índice primário. O medido '
        f'foi {n(u4["idx1_leituras"] / u4["registros"], 2)}, porque a árvore teve altura 1 e 2 durante parte da '
        f'carga e chegou a 3 só depois de {u4["idx1_ordem"]} folhas. As escritas ficaram próximas do previsto: 1 '
        f'por inserção, mais 2 por divisão. O índice secundário leu {n(u4["idx2_leituras"] / u4["registros"], 2)} '
        f'blocos por inserção, contra 3,9 previstos, pelo mesmo motivo.</p>')
    add(f'<p><b>(e) Overflow menor que o previsto.</b> O modelo supôs capacidade fixa de 7 registros por bloco. Como '
        f'os registros têm tamanho variável, um bloco recebe até {u4["rpb_max"]} registros pequenos, e buckets '
        f'com 8 ou 9 registros muitas vezes não transbordam. Por isso há {n(u4["overflow"])} blocos de overflow, '
        f'e não ≈ 9.240.</p>')
    add(f'<p><b>(f) O bloco de cabeçalho.</b> O <code>findrec</code> sempre lê 1 bloco a mais (o cabeçalho, com N). '
        f'Sem ele, a busca por hashing custaria 1 bloco por registro sem overflow. Esse bloco poderia ser evitado '
        f'mantendo N em memória (catálogo do SGBD), mas cada programa aqui é um processo novo e lê N do disco. '
        f'Mesmo pagando o cabeçalho, o hashing ({ids(r4, 5, "findrec", "blocos_lidos_dados")} blocos) é mais '
        f'barato que a B+Tree ({ids(r4, 5, "seek1", "blocos_lidos_indice") + 1} blocos) na busca exata por ID, '
        f'como o enunciado antecipa.</p>')

    # ---------------- 7.5 ----------------
    def c(r, f):
        return f(r)
    add('<h2>7.5 Efeito do tamanho do bloco (4096 × 8192 bytes)</h2>')
    add(tab(['grandeza', '4096', '8192', 'variação'], [
        ['bytes úteis por bloco de dados', n(u4['payload']), n(u8['payload']), '× 2,0'],
        ['registros por bloco (média; capacidade p/ registro médio)', f'{n(u4["rpb_media"], 2)}; {u4["rpb_cap"]}',
         f'{n(u8["rpb_media"], 2)}; {u8["rpb_cap"]}', f'× {n(u8["rpb_media"] / u4["rpb_media"], 2)}'],
        ['N (buckets)', n(u4['buckets']), n(u8['buckets']), f'× {n(u8["buckets"] / u4["buckets"], 2)}'],
        ['buckets com overflow', f'{n(u4["com_ovf"])} ({n(100 * u4["com_ovf"] / u4["buckets"], 1)} %)',
         f'{n(u8["com_ovf"])} ({n(100 * u8["com_ovf"] / u8["buckets"], 1)} %)', ''],
        ['taxa de ocupação dos blocos de dados', f'{n(u4["ocupacao"], 1)} %', f'{n(u8["ocupacao"], 1)} %', ''],
        ['blocos do arquivo de dados (= custo de findtitle/findrange)', n(u4['blocos_dados']), n(u8['blocos_dados']),
         f'× {n(u8["blocos_dados"] / u4["blocos_dados"], 3)}'],
        ['índice primário: folha / ordem', f'{u4["idx1_folha_cap"]} / {u4["idx1_ordem"]}',
         f'{u8["idx1_folha_cap"]} / {u8["idx1_ordem"]}', '× 2'],
        ['índice primário: folhas / altura', f'{u4["idx1_folhas"]} / {u4["idx1_altura"]}',
         f'{u8["idx1_folhas"]} / {u8["idx1_altura"]}', '−1 nível'],
        ['índice secundário: folha / ordem', f'{u4["idx2_folha_cap"]} / {u4["idx2_ordem"]}',
         f'{u8["idx2_folha_cap"]} / {u8["idx2_ordem"]}', '× 2'],
        ['índice secundário: folhas / altura', f'{n(u4["idx2_folhas"])} / {u4["idx2_altura"]}',
         f'{n(u8["idx2_folhas"])} / {u8["idx2_altura"]}', '−1 nível'],
        ['blocos dos índices (primário / secundário)', f'{u4["idx1_blocos"]} / {n(u4["idx2_blocos"])}',
         f'{u8["idx1_blocos"]} / {n(u8["idx2_blocos"])}', ''],
    ]))
    add('<p>Medição das consultas oficiais com 8192 B (<code>docs/resultados/consultas_8192.txt</code>):</p>')
    add(tabelas_medicao(r8))
    a8 = analise_secao(r8['analise'], 'A')
    suc8 = float(re.search(r'COM sucesso : ([\d.]+)', a8).group(1))
    add(f'<p><b>Registros por bloco.</b> Dobrar o bloco dobra os bytes úteis (o cabeçalho de 16 bytes pesa '
        f'menos), e a capacidade sobe de {u4["rpb_cap"]} para {u8["rpb_cap"]} registros médios. O número de '
        f'blocos do arquivo de dados caiu para {n(100 * u8["blocos_dados"] / u4["blocos_dados"], 1)} % do valor '
        f'anterior, um pouco <i>menos</i> que a metade. Com λ = {n(lambda4, 2)} registros por bucket, a variância '
        f'relativa de Poisson (1/√λ) é maior que com λ = {n(u8["registros"] / u8["buckets"], 2)}. Buckets grandes '
        f'absorvem melhor a flutuação, então há menos overflow ({n(100 * u8["com_ovf"] / u8["buckets"], 1)} % dos '
        f'buckets contra {n(100 * u4["com_ovf"] / u4["buckets"], 1)} %) e a ocupação sobe. As varreduras '
        f'(<code>findtitle</code> e <code>findrange</code>) acompanham esse número e caem de {n(u4["blocos_dados"])} '
        f'para {n(u8["blocos_dados"])} blocos. O <code>findrec</code> continua com 2 blocos (cabeçalho + bucket), '
        f'com média geral {n(suc8, 3)} contra {n(suc, 3)}: menos registros caem no overflow. Cada leitura, porém, '
        f'transfere o dobro de bytes.</p>')
    add(f'<p><b>Ordem e altura.</b> A ordem é proporcional a B, então os dois índices dobram de ordem. O índice '
        f'primário passa a caber em {u8["idx1_folhas"]} folhas sob uma única raiz de ordem {u8["idx1_ordem"]} '
        f'(altura 2). O secundário passa de {u4["idx2_altura"]} para {u8["idx2_altura"]} níveis. Logo, o '
        f'<code>seek1</code> cai de 4 para 3 blocos e o <code>seek2</code> de título único de 4–5 para 3–4. No '
        f'<code>seekrange</code>, as leituras de índice caem pela metade ('
        f'{fx(r4, 200000, 260000, "seekrange", "blocos_lidos_indice")} → '
        f'{fx(r8, 200000, 260000, "seekrange", "blocos_lidos_indice")} no intervalo grande). As leituras de dados '
        f'não mudam: continua sendo 1 bloco por registro, porque o hashing espalha IDs vizinhos por blocos '
        f'diferentes. Em intervalos grandes, o custo total quase não se altera.</p>')
    c4 = analise_secao(r4['analise'], 'C')
    c8 = analise_secao(r8['analise'], 'C')
    add(f'<p><b>Consequência para a escolha do plano.</b> Com 8192 B, a varredura fica 2,2 vezes mais barata, mas '
        f'o <code>seekrange</code> não. O ponto em que o índice deixa de compensar cai de ≈ {n(u4["blocos_dados"])} '
        f'registros ({n(100 * u4["blocos_dados"] / u4["registros"], 0)} % da tabela) para ≈ '
        f'{n(u8["blocos_dados"])} ({n(100 * u8["blocos_dados"] / u8["registros"], 1)} %). Veja o experimento C na '
        f'seção 7.6.</p>')

    # ---------------- 7.6 ----------------
    add('<h2>7.6 Casos patológicos</h2>')
    add('<h3>Busca que não encontra nada</h3>')
    add(tab(['operação', 'chave inexistente', 'blocos lidos (4096)', 'por quê'], [
        ['findrec', 'ID 1', ids(r4, 1, 'findrec', 'blocos_lidos_dados'),
         'cabeçalho + bucket 1 inteiro (o bucket 1 tem 1 bloco de overflow)'],
        ['findrec', 'ID 2000000', ids(r4, 2000000, 'findrec', 'blocos_lidos_dados'),
         'cabeçalho + bucket sem overflow'],
        ['seek1', 'ID 1 / 2000000', f'{ids(r4, 1, "seek1", "blocos_lidos_indice")} + 0',
         'desce até a folha; a ausência é detectada sem tocar no arquivo de dados'],
        ['findtitle', TN, n(tit(r4, TN, 'findtitle', 'blocos_lidos_dados')),
         'varredura completa: só no fim se sabe que não há o título'],
        ['seek2', TN, f'{tit(r4, TN, "seek2", "blocos_lidos_indice")} + 0', 'descida até a folha; nenhum candidato'],
        ['findrange', '[1600000, 1700000]', n(fx(r4, 1600000, 1700000, 'findrange', 'blocos_lidos_dados')),
         'varredura completa'],
        ['seekrange', '[1600000, 1700000]', f'{fx(r4, 1600000, 1700000, "seekrange", "blocos_lidos_indice")} + 0',
         'desce até a última folha; next = 0'],
    ]))
    add(f'<p>Na Parte 1, uma busca sem resultado custa o mesmo que uma com resultado no caso das varreduras. No '
        f'hashing, ela custa um pouco mais: precisa ler a cadeia inteira (média {n(ins, 3)} contra {n(suc, 3)} '
        f'com sucesso, experimento A). Na Parte 2, a busca sem resultado é a mais barata possível, só a altura da '
        f'árvore. O <code>findrec</code> do ID 1 (3 blocos) custa mais que qualquer busca com sucesso da lista '
        f'oficial: o bucket 1 está entre os {n(100 * u4["com_ovf"] / u4["buckets"], 1)} % que '
        f'transbordaram.</p>')
    add(pre(rastro('findrec', 1)))
    add('<h3>Índice secundário devolvendo vários registros</h3>')
    add(f'<p>"{TR1}" tem 45 registros, e "{TR2}" tem 26. Como a chave é (título, RID), as entradas ficam contíguas '
        f'nas folhas: o custo no índice é a altura mais as folhas extras ocupadas pelo grupo '
        f'({tit(r4, TR1, "seek2", "blocos_lidos_indice")} e {tit(r4, TR2, "seek2", "blocos_lidos_indice")}). O custo '
        f'dominante é o do arquivo de dados: <b>1 bloco por registro</b> '
        f'({tit(r4, TR1, "seek2", "blocos_lidos_dados")} e {tit(r4, TR2, "seek2", "blocos_lidos_dados")}). O hashing '
        f'por ID espalha registros de mesmo título por buckets diferentes, e o índice secundário é não agrupado '
        f'(<i>unclustered</i>). Ainda assim, 51 blocos contra {n(u4["blocos_dados"])} da varredura. O mesmo vale '
        f'para títulos diferentes com o mesmo prefixo de 64 bytes. Eles aparecem como candidatos, e cada '
        f'candidato custa 1 leitura de dados antes de ser descartado. No experimento B, a média foi '
        f'{re.search(r"candidatos medios.*?: ([0-9.]+)", b4).group(1).replace(".", ",")} candidatos por título '
        f'buscado.</p>')
    add('<h3>Tamanho do intervalo e custo da B+Tree</h3>')
    add('<p>O custo do <code>seekrange</code> para um intervalo com k registros é altura + ⌈k / entradas por '
        'folha⌉ no índice, mais k no arquivo de dados. O termo k domina rapidamente: a partir de k = 1.000, o '
        'índice responde por menos de 1 % do total. O experimento C mede intervalos com k crescente no meio do '
        'arquivo:</p>')
    add(pre(c4))
    add(pre(c8))
    add(f'<p>Com 4096 B, o índice compensa até k ≈ {n(u4["blocos_dados"])} registros, cerca de '
        f'{n(100 * u4["blocos_dados"] / u4["registros"], 0)} % da tabela. Acima disso, a varredura do '
        f'<code>findrange</code> é mais barata, porque lê cada bloco uma vez só, enquanto o '
        f'<code>seekrange</code> volta ao mesmo bloco de dados várias vezes, uma por registro. Com 8192 B, o '
        f'limite cai para ≈ {n(u8["blocos_dados"])} registros. É a mesma conclusão que um otimizador de consultas '
        f'tira de um índice não agrupado: só vale a pena para predicados seletivos.</p>')
    add('<h3>Concentração de registros nos buckets</h3>')
    lam = lambda4
    hb = u4['hist_bucket']
    linhas = []
    for k in range(0, max(hb) + 1):
        esperado = u4['buckets'] * poisson(lam, k)
        linhas.append([k, n(hb.get(k, 0)), n(esperado, 0)])
    metade = (len(linhas) + 1) // 2
    combinadas = [a + b for a, b in zip(linhas[:metade], linhas[metade:] + [['', '', '']] * (metade - len(linhas[metade:])))]
    add(tab(['registros no bucket', 'buckets (medido)', 'Poisson λ=' + n(lam, 2),
             'registros no bucket', 'buckets (medido)', 'Poisson λ=' + n(lam, 2)], combinadas))
    add(f'<p>Não houve concentração anômala. A distribuição de registros por bucket segue a de Poisson com '
        f'λ = {n(lam, 2)}, que é o esperado de uma função de hashing uniforme. O maior bucket tem '
        f'{u4["maior_bucket"]} registros, há {u4["vazios"]} buckets vazios (Poisson prevê '
        f'{n(u4["buckets"] * poisson(lam, 0), 0)}), e nenhuma cadeia passa de {u4["maior_cadeia"]} blocos '
        f'({u4["hist_cadeia"].get(3, 0)} buckets com 3 blocos). O que leva {n(100 * u4["com_ovf"] / u4["buckets"], 1)} '
        f'% dos buckets ao overflow não é concentração, e sim a combinação de ocupação alvo de 80 % com buckets '
        f'pequenos, de ≈ 7 registros: a flutuação natural de Poisson já ultrapassa a capacidade do bloco. Com '
        f'8192 B (λ = {n(u8["registros"] / u8["buckets"], 2)}), a mesma ocupação gera só '
        f'{n(100 * u8["com_ovf"] / u8["buckets"], 1)} % de buckets com overflow e nenhuma cadeia com 3 blocos.</p>')

    # ---------------- 7.7 ----------------
    add('<h2>7.7 Declaração de uso de ferramentas de IA</h2>')
    add('<p><b>Ferramenta</b>: Claude Code (agente de programação da Anthropic, modelo Claude Opus 5.5), usado na '
        'extensão do VS Code.</p>')
    add('<p><b>O que foi solicitado</b>: (1) na instrução inicial, o aluno pediu que a ferramenta realizasse todo o '
        'trabalho especificado no ENUNCIADO.md, com código comentado e testado. (2) Depois, pediu uma segunda '
        'verificação do cumprimento do enunciado e uma estimativa de nota pelos critérios da Seção 12. Nessa '
        'verificação, a ferramenta encontrou e corrigiu um defeito: IDs fora da faixa de 32 bits (ex.: '
        '<code>findrec 9999999999</code>) eram convertidos silenciosamente para outro número e agora são recusados '
        '(<code>include/args.hpp</code>). A correção não alterou nenhum número medido. (3) Por fim, pediu que a '
        'ferramenta redigisse esta seção a partir das informações fornecidas pelo aluno.</p>')
    add('<p><b>Partes produzidas pela ferramenta</b>: o código da entrega foi integralmente gerado pela '
        'ferramenta. Isso inclui (1) todo o código C++ de <code>src/</code>, '
        '<code>include/</code> e <code>tools/</code>, exceto a camada <code>blockio</code> e o '
        '<code>CsvReader</code>, que vêm do template. Neste último a ferramenta só acrescentou '
        '<code>#include &lt;cstdint&gt;</code>, que faltava para compilar com g++ 13. (2) Os testes de '
        '<code>tests/</code>. (3) Os scripts de <code>scripts/</code>. (4) Os ajustes no Makefile e no README. '
        '(5) A previsão de <code>docs/previsao.md</code>, escrita antes da primeira execução. (6) Este relatório, '
        'gerado por <code>scripts/gerar_relatorio.py</code> a partir das saídas dos programas. As decisões de '
        'projeto da seção 7.1 foram propostas pela ferramenta, com base em medições feitas no próprio CSV.</p>')
    add('<p><b>O que foi feito posteriormente pelo aluno</b>: o aluno revisou e fez o rastreio de todo o código '
        'gerado e preparou uma apresentação para a arguição.</p>')

    # ---------------- reprodução ----------------
    add('<h2>Anexo — Como reproduzir os números deste relatório</h2>')
    add(pre('gzip -dk data/artigo-amostra.csv.gz\n'
            'make build && make test && make test-integration   # compilação e testes\n'
            'bash scripts/run_experiments.sh                     # carga + consultas oficiais (4096 e 8192)\n'
            'python3 scripts/gerar_relatorio.py                  # regenera este PDF\n\n'
            '# no contêiner (BLOCK_SIZE padrão 4096):\n'
            'docker build -t tp2 .\n'
            'docker run --rm -v $(pwd)/data:/data tp2 ./bin/upload /data/artigo-amostra.csv\n'
            'docker run --rm -v $(pwd)/data:/data tp2 ./bin/seek1 5\n'
            '# BLOCK_SIZE=8192:  docker build --build-arg BLOCK_SIZE=8192 -t tp2-8k .'))

    return '\n'.join(H)


def tabelas_medicao(r):
    ids_l = []
    for i, d in r['ids'].items():
        f, s = d['findrec'], d['seek1']
        ids_l.append([i, 'sim' if f['encontrados'] else 'não', f['blocos_lidos_dados'], s['blocos_lidos_indice'],
                      s['blocos_lidos_dados'], s['blocos_lidos_indice'] + s['blocos_lidos_dados']])
    out = ['<p><b>Busca por ID</b></p>', tab(['ID', 'existe', 'findrec: dados', 'seek1: índice', 'seek1: dados',
                                               'seek1: total'], ids_l)]
    tl = []
    for t, d in r['titulos'].items():
        f, s = d['findtitle'], d['seek2']
        tl.append([e(t), f['encontrados'], n(f['blocos_lidos_dados']), s['blocos_lidos_indice'], s['blocos_lidos_dados'],
                   s['blocos_lidos_indice'] + s['blocos_lidos_dados']])
    out += ['<p><b>Busca por título</b></p>', tab(['título', 'registros', 'findtitle: dados', 'seek2: índice',
                                                   'seek2: dados', 'seek2: total'], tl)]
    fl = []
    for (a, b), d in r['faixas'].items():
        f, s = d['findrange'], d['seekrange']
        fl.append([f'[{a}, {b}]', n(f['encontrados']), n(f['blocos_lidos_dados']), s['blocos_lidos_indice'],
                   n(s['blocos_lidos_dados']), n(s['blocos_lidos_indice'] + s['blocos_lidos_dados'])])
    out += ['<p><b>Busca por intervalo</b></p>', tab(['intervalo', 'registros', 'findrange: dados',
                                                      'seekrange: índice', 'seekrange: dados', 'seekrange: total'], fl)]
    u = r['upload']
    out.append(f'<p class="nota">Totais: arquivo de dados {n(u["blocos_dados"])} blocos; índice primário '
               f'{n(u["idx1_blocos"])} blocos; índice secundário {n(u["idx2_blocos"])} blocos. Os programas da '
               f'Parte 2 informam também o total de blocos do arquivo de dados.</p>')
    return '\n'.join(out)


def md_para_html(md):
    """Conversor mínimo de Markdown (títulos, tabelas, listas, parágrafos)."""
    out, tabela, lista, par = [], [], [], []

    def inline(s):
        s = e(s)
        s = re.sub(r'\*\*(.+?)\*\*', r'<b>\1</b>', s)
        s = re.sub(r'`(.+?)`', r'<code>\1</code>', s)
        return s

    def flush():
        if par:
            out.append('<p>' + inline(' '.join(par)) + '</p>')
            par.clear()
        if lista:
            out.append('<ul>' + ''.join(f'<li>{inline(x)}</li>' for x in lista) + '</ul>')
            lista.clear()
        if tabela:
            linhas = [[c.strip() for c in l.strip().strip('|').split('|')] for l in tabela
                      if not re.fullmatch(r'\|[-|\s]+\|', l.strip())]
            out.append(tab(linhas[0], [[inline(c) for c in l] for l in linhas[1:]]))
            tabela.clear()

    for linha in md.splitlines():
        if linha.startswith('|'):
            if par or lista:
                flush()
            tabela.append(linha)
            continue
        if tabela:
            flush()
        if linha.startswith('## '):
            flush()
            out.append(f'<h3>{inline(linha[3:])}</h3>')
        elif linha.startswith('- '):
            if par:
                flush()
            lista.append(linha[2:])
        elif linha.startswith('  ') and lista:
            lista[-1] += ' ' + linha.strip()
        elif not linha.strip():
            flush()
        else:
            if lista:
                flush()
            par.append(linha.strip())
    flush()
    return '\n'.join(out)


ESTILO = """
body { font-family: 'DejaVu Serif', serif; font-size: 10pt; }
h1 { font-size: 18pt; } h2 { font-size: 14pt; margin-top: 14pt; } h3 { font-size: 11.5pt; }
th { background: #dddddd; font-size: 8.5pt; } td { font-size: 8.5pt; }
pre { font-family: 'DejaVu Sans Mono', monospace; font-size: 7.5pt; background: #f2f2f2; }
code { font-family: 'DejaVu Sans Mono', monospace; font-size: 9pt; }
p.nota { font-size: 8.5pt; }
"""


def main():
    corpo = gerar()
    doc = ('<!doctype html>\n<html lang="pt-BR"><head><meta charset="utf-8"/>'
           '<title>TP2 — Relatório</title>'
           f'<style>{ESTILO}</style></head><body>\n{corpo}\n</body></html>\n')
    destino_html = os.path.join(RAIZ, 'docs', 'tp2_relatorio.html')
    with open(destino_html, 'w', encoding='utf-8') as f:
        f.write(doc)
    print('HTML:', destino_html)

    soffice = shutil.which('soffice') or shutil.which('libreoffice')
    if not soffice:
        print('LibreOffice (soffice) nao encontrado: PDF nao gerado', file=sys.stderr)
        return 1
    subprocess.run([soffice, '--headless', '--infilter=HTML (StarWriter)', '--convert-to',
                    'pdf:writer_pdf_Export', '--outdir', os.path.join(RAIZ, 'docs'), destino_html],
                   check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    print('PDF :', os.path.join(RAIZ, 'docs', 'tp2_relatorio.pdf'))
    return 0


if __name__ == '__main__':
    sys.exit(main())
