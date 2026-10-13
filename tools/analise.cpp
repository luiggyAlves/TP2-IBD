// analise — experimentos complementares para o relatório (Seções 7.4 e 7.6).
//
// Não é um dos 7 programas pedidos; serve para explicar os números medidos.
// Usa os arquivos já criados pelo upload (em TP2_DB_DIR, padrão /data/db) e
// faz todo acesso pela camada blockio, como os demais programas.
//
// Experimentos:
//   A. Custo de findrec para TODOS os IDs (média de sucesso e de insucesso),
//      a partir da posição de cada registro na cadeia do seu bucket.
//   B. Custo de seek2 para uma amostra de títulos distintos (média, máximo).
//   C. Custo de seekrange em função do tamanho do intervalo, comparado com a
//      varredura do findrange (ponto de equilíbrio).
//   D. Efeito da ordem de inserção na ocupação e na altura do índice primário:
//      ordem física do arquivo hash (a usada no upload), ordem crescente de ID
//      e ordem aleatória. As árvores de teste são gravadas em arquivos
//      temporários no mesmo diretório e apagadas ao final.
//
// Uso: analise   (com TP2_DB_DIR apontando para os arquivos do upload)

#include <algorithm>
#include <cstdio>
#include <map>
#include <random>
#include <string>
#include <vector>

#include "blockio.hpp"
#include "bptree.hpp"
#include "config.hpp"
#include "datafile.hpp"

using namespace tp2;

namespace {

struct Entrada {
    int id;
    Rid rid;
    std::string titulo;
};

// ---------------------------------------------------------------------------
// A. findrec: custo médio sobre todas as chaves
// ---------------------------------------------------------------------------
void experimento_a(HashFile& hf, std::uint64_t total_blocos) {
    const std::uint32_t n = hf.header().num_buckets;
    std::vector<std::uint8_t> buf(blockio::kBlockSize);
    std::map<std::uint32_t, std::uint64_t> por_posicao;  // posição na cadeia -> registros
    std::map<std::uint32_t, std::uint64_t> por_cadeia;   // comprimento da cadeia -> buckets
    std::uint64_t soma_sucesso = 0, registros = 0, soma_insucesso = 0;

    // Percorre cada bucket seguindo a cadeia, como faria o findrec.
    for (std::uint32_t b = 0; b < n; ++b) {
        std::uint32_t blk = bucket_block(b), pos = 0;
        while (blk != 0) {
            hf.file().read_block(blk, buf.data());
            ++pos;
            const DataBlockHeader h = get_block_header(buf.data());
            por_posicao[pos] += h.count;
            soma_sucesso += static_cast<std::uint64_t>(h.count) * (1 + pos);  // +1 = cabeçalho
            registros += h.count;
            blk = h.next;
        }
        ++por_cadeia[pos];
        soma_insucesso += 1 + pos;  // insucesso lê a cadeia inteira
    }
    std::printf("\n[A] findrec sobre todas as chaves (custo = 1 cabecalho + posicao na cadeia)\n");
    for (const auto& [p, c] : por_posicao)
        std::printf("    registros no %u. bloco da cadeia: %llu (%.2f%%) -> custo %u\n", p,
                    static_cast<unsigned long long>(c), 100.0 * c / registros, 1 + p);
    std::printf("    custo medio de busca COM sucesso : %.4f blocos\n", static_cast<double>(soma_sucesso) / registros);
    std::printf("    custo medio de busca SEM sucesso : %.4f blocos (ID que cai em bucket uniforme)\n",
                static_cast<double>(soma_insucesso) / n);
    for (const auto& [c, q] : por_cadeia)
        std::printf("    buckets com cadeia de %u bloco(s): %llu (%.2f%%)\n", c, static_cast<unsigned long long>(q),
                    100.0 * q / n);
    std::printf("    total de blocos do arquivo: %llu\n", static_cast<unsigned long long>(total_blocos));
}

// ---------------------------------------------------------------------------
// B. seek2: custo para uma amostra de títulos
// ---------------------------------------------------------------------------
void experimento_b(const std::vector<Entrada>& entradas) {
    std::map<std::string, int> titulos;
    for (const Entrada& e : entradas) ++titulos[e.titulo];
    std::vector<std::string> distintos;
    distintos.reserve(titulos.size());
    for (const auto& kv : titulos) distintos.push_back(kv.first);
    std::mt19937 rng(2026);
    std::shuffle(distintos.begin(), distintos.end(), rng);
    const std::size_t amostra = std::min<std::size_t>(5000, distintos.size());

    BPlusTree idx;
    idx.open(secondary_index_path());
    std::map<std::uint64_t, std::uint64_t> hist;  // leituras no índice -> títulos
    std::uint64_t soma = 0, soma_cand = 0, maximo = 0;
    std::vector<std::uint8_t> chave(kTitleKeySize);
    for (std::size_t i = 0; i < amostra; ++i) {
        idx.reset_stats();
        encode_title_search_key(distintos[i], chave.data());
        std::uint64_t cand = 0;
        for (BPlusTree::Cursor c = idx.lower_bound(chave.data()); c.valid; idx.next(c)) {
            if (!title_prefix_equal(idx.key_at(c), chave.data())) break;
            ++cand;
        }
        const std::uint64_t r = idx.stats().reads;
        ++hist[r];
        soma += r;
        soma_cand += cand;
        maximo = std::max(maximo, r);
    }
    std::printf("\n[B] seek2 para %zu titulos distintos sorteados (altura %u)\n", amostra, idx.height());
    std::printf("    leituras medias no indice: %.4f (maximo %llu)\n", static_cast<double>(soma) / amostra,
                static_cast<unsigned long long>(maximo));
    std::printf("    candidatos medios (= leituras no arquivo de dados): %.4f\n",
                static_cast<double>(soma_cand) / amostra);
    for (const auto& [r, q] : hist)
        std::printf("    %llu leituras no indice: %llu titulos (%.2f%%)\n", static_cast<unsigned long long>(r),
                    static_cast<unsigned long long>(q), 100.0 * q / amostra);
}

// ---------------------------------------------------------------------------
// C. seekrange x findrange em função do número de registros do intervalo
// ---------------------------------------------------------------------------
void experimento_c(std::vector<int> ids, std::uint64_t blocos_dados) {
    std::sort(ids.begin(), ids.end());
    BPlusTree idx;
    idx.open(primary_index_path());
    blockio::BlockFile dados;
    dados.open(data_path(), blockio::Mode::kRead);
    std::printf("\n[C] seekrange em funcao do numero k de registros do intervalo "
                "(findrange le sempre %llu blocos)\n",
                static_cast<unsigned long long>(blocos_dados));
    std::printf("    %10s %14s %14s %14s %12s\n", "k", "indice", "dados", "total", "total/scan");
    const std::vector<std::size_t> tamanhos = {1, 10, 100, 1000, 5000, 10000, 20000, 30000, 40000, 60000, 100000};
    std::uint8_t chave[kIdKeySize];
    Article a;
    for (std::size_t k : tamanhos) {
        if (k > ids.size()) break;
        // Intervalo que começa no meio do arquivo e contém exatamente k IDs.
        const std::size_t ini = (ids.size() - k) / 2;
        const int lo = ids[ini], hi = ids[ini + k - 1];
        idx.reset_stats();
        dados.reset_stats();
        encode_id_key(lo, chave);
        std::size_t achados = 0;
        for (BPlusTree::Cursor c = idx.lower_bound(chave); c.valid; idx.next(c)) {
            if (decode_id_key(idx.key_at(c)) > hi) break;
            read_record_at(dados, idx.rid_at(c), a);
            ++achados;
        }
        const auto li = idx.stats().reads, ld = dados.stats().reads;
        std::printf("    %10zu %14llu %14llu %14llu %11.3f%s\n", achados, static_cast<unsigned long long>(li),
                    static_cast<unsigned long long>(ld), static_cast<unsigned long long>(li + ld),
                    static_cast<double>(li + ld) / blocos_dados, li + ld > blocos_dados ? "  <- pior que a varredura" : "");
    }
}

// ---------------------------------------------------------------------------
// D. Ordem de inserção x ocupação/altura do índice primário
// ---------------------------------------------------------------------------
void experimento_d(const std::vector<Entrada>& ordem_fisica) {
    std::printf("\n[D] indice primario construido com 3 ordens de insercao diferentes\n");
    std::printf("    %-32s %8s %8s %8s %10s %12s %12s\n", "ordem", "altura", "folhas", "internos", "ocup.folha",
                "leituras", "escritas");
    auto constroi = [&](const char* nome, const std::vector<Entrada>& ordem) {
        const std::string path = db_dir() + "/tmp_analise.bpt";
        BPlusTree t;
        t.create(path, kIdKeySize);
        std::uint8_t chave[kIdKeySize];
        for (const Entrada& e : ordem) {
            encode_id_key(e.id, chave);
            t.insert(chave, e.rid);
        }
        t.finish();
        std::printf("    %-32s %8u %8u %8u %9.1f%% %12llu %12llu\n", nome, t.height(), t.leaves(), t.internals(),
                    100.0 * t.entries() / (static_cast<double>(t.leaves()) * BPlusTree::leaf_capacity(kIdKeySize)),
                    static_cast<unsigned long long>(t.stats().reads), static_cast<unsigned long long>(t.stats().writes));
        std::remove(path.c_str());
    };
    constroi("fisica do arquivo hash (upload)", ordem_fisica);
    std::vector<Entrada> crescente = ordem_fisica;
    std::sort(crescente.begin(), crescente.end(), [](const Entrada& x, const Entrada& y) { return x.id < y.id; });
    constroi("crescente de ID (ordem do CSV)", crescente);
    std::vector<Entrada> aleatoria = ordem_fisica;
    std::mt19937 rng(7);
    std::shuffle(aleatoria.begin(), aleatoria.end(), rng);
    constroi("aleatoria", aleatoria);
}

}  // namespace

int main() {
    try {
        HashFile hf;
        hf.open(data_path());
        const std::uint64_t total = hf.file().block_count();
        std::printf("== analise (BLOCK_SIZE=%zu, %s) ==\n", blockio::kBlockSize, db_dir().c_str());

        experimento_a(hf, total);

        // Lista (ID, RID, título) na ordem física, usada por B, C e D.
        std::vector<Entrada> entradas;
        Article a;
        hf.scan([&](const Rid& rid, const std::uint8_t* rec) {
            decode_record(rec, a);
            entradas.push_back(Entrada{a.id, rid, a.titulo});
        });
        experimento_b(entradas);
        std::vector<int> ids;
        for (const Entrada& e : entradas) ids.push_back(e.id);
        experimento_c(ids, total);
        experimento_d(entradas);
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "erro: %s\n", e.what());
        return 1;
    }
}
