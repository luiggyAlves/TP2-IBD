// upload <arquivo.csv> — carga inicial dos dados (Partes 1 e 2).
//
// Etapas:
//   1. 1ª passada no CSV: conta os registros e soma seus tamanhos em disco,
//      para dimensionar o número de buckets N do hashing.
//   2. Cria o arquivo de dados com N buckets (bloco 0 = cabeçalho,
//      blocos 1..N = buckets) e, na 2ª passada no CSV, insere cada registro
//      no bucket h(ID) = ID mod N, alocando blocos de overflow quando preciso.
//   3. Constrói os dois índices B+Tree varrendo o arquivo de dados recém-criado
//      (é dessa varredura que saem os RIDs) e inserindo, registro a registro,
//      (ID, RID) no índice primário e (Título, RID) no secundário.
//
// O CSV é lido com o CsvReader fornecido (ifstream), o que é permitido: ele é
// apenas a entrada da carga. Os arquivos de dados e de índice são escritos e
// lidos exclusivamente pela camada blockio, e todos os blocos são contados.

#include <sys/stat.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

#include "blockio.hpp"
#include "bptree.hpp"
#include "config.hpp"
#include "csv_reader.hpp"
#include "datafile.hpp"

using namespace tp2;

namespace {

// Cria o diretório 'path' e os intermediários que faltarem (como mkdir -p).
void make_dirs(const std::string& path) {
    for (std::size_t i = 1; i <= path.size(); ++i) {
        if (i == path.size() || path[i] == '/') {
            const std::string parte = path.substr(0, i);
            if (::mkdir(parte.c_str(), 0755) != 0 && errno != EEXIST)
                throw std::runtime_error("nao foi possivel criar o diretorio " + parte + ": " + std::strerror(errno));
        }
    }
}

double pct(double parte, double total) { return total > 0 ? 100.0 * parte / total : 0.0; }

unsigned long long ull(std::uint64_t v) { return static_cast<unsigned long long>(v); }

// Imprime as estatísticas de um índice já construído.
void print_index_stats(const char* nome, const BPlusTree& t, std::uint64_t entradas_tentadas) {
    const std::uint64_t nos = t.block_count();
    std::printf("[%s] %s\n", nome, t.path().c_str());
    std::printf("  tamanho da chave             : %zu bytes\n", t.key_size());
    std::printf("  entrada de folha             : %zu bytes (chave + RID de 6 bytes)\n",
                t.key_size() + BPlusTree::kRidSize);
    std::printf("  capacidade da folha          : %zu entradas\n", BPlusTree::leaf_capacity(t.key_size()));
    std::printf("  ordem do no interno          : %zu filhos (%zu chaves)\n",
                BPlusTree::internal_capacity(t.key_size()) + 1, BPlusTree::internal_capacity(t.key_size()));
    std::printf("  entradas inseridas           : %u (de %llu; %llu duplicadas recusadas)\n", t.entries(),
                ull(entradas_tentadas), ull(entradas_tentadas - t.entries()));
    std::printf("  altura                       : %u niveis (raiz ... folhas)\n", t.height());
    std::printf("  blocos (nos) do indice       : %llu (folhas %u, internos %u)\n", ull(nos), t.leaves(),
                t.internals());
    std::printf("  ocupacao media das folhas    : %.1f%% (%.1f entradas por folha)\n",
                pct(t.entries(), static_cast<double>(t.leaves()) * BPlusTree::leaf_capacity(t.key_size())),
                t.leaves() ? static_cast<double>(t.entries()) / t.leaves() : 0.0);
    std::printf("  custo da construcao          : %llu blocos lidos, %llu blocos escritos\n",
                ull(t.stats().reads), ull(t.stats().writes));
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "uso: %s <arquivo.csv>\n", argv[0]);
        return 2;
    }
    const std::string csv_path = argv[1];
    const auto inicio = std::chrono::steady_clock::now();

    try {
        make_dirs(db_dir());

        // ------------------------------------------------------------------
        // 1ª passada: dimensionamento
        // ------------------------------------------------------------------
        std::uint64_t n_registros = 0, bytes_total = 0, truncados = 0;
        std::size_t menor_reg = SIZE_MAX, maior_reg = 0;
        {
            CsvReader reader(csv_path);
            Article a;
            while (reader.next(a)) {
                if (fit_record(a, kDataPayload)) ++truncados;
                const std::size_t s = record_size(a);
                bytes_total += s;
                menor_reg = std::min(menor_reg, s);
                maior_reg = std::max(maior_reg, s);
                ++n_registros;
            }
        }
        if (n_registros == 0) throw std::runtime_error("nenhum registro valido no CSV " + csv_path);

        // N = menor primo >= bytes / (bytes úteis por bloco * ocupação alvo).
        const double buckets_ideais = std::ceil(static_cast<double>(bytes_total) / (kDataPayload * kHashLoadFactor));
        const std::uint32_t num_buckets = next_prime(static_cast<std::uint32_t>(std::max(1.0, buckets_ideais)));

        // ------------------------------------------------------------------
        // 2ª passada: gravação do arquivo de dados por hashing
        // ------------------------------------------------------------------
        HashFile hf;
        hf.create(data_path(), num_buckets);
        std::uint64_t ignoradas = 0;
        {
            CsvReader reader(csv_path);
            Article a;
            while (reader.next(a)) {
                fit_record(a, kDataPayload);
                hf.insert(a);
            }
            ignoradas = reader.skipped();
        }
        const LoadStats ls = hf.finish();
        const blockio::Stats custo_dados = hf.file().stats();

        // ------------------------------------------------------------------
        // Construção dos índices (varredura do arquivo de dados)
        // ------------------------------------------------------------------
        hf.file().reset_stats();  // a partir daqui, conta só a varredura para os índices
        BPlusTree primario, secundario;
        primario.create(primary_index_path(), kIdKeySize);
        secundario.create(secondary_index_path(), kTitleKeySize);

        std::uint64_t entradas = 0;
        std::vector<std::uint8_t> chave_id(kIdKeySize), chave_tit(kTitleKeySize);
        Article a;
        hf.scan([&](const Rid& rid, const std::uint8_t* rec) {
            decode_record(rec, a);
            encode_id_key(a.id, chave_id.data());
            primario.insert(chave_id.data(), rid);
            encode_title_key(a.titulo, rid, chave_tit.data());
            secundario.insert(chave_tit.data(), rid);
            ++entradas;
        });
        primario.finish();
        secundario.finish();
        const blockio::Stats custo_varredura = hf.file().stats();

        const double ms =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - inicio).count();

        // ------------------------------------------------------------------
        // Relatório
        // ------------------------------------------------------------------
        const double media_reg = static_cast<double>(ls.record_bytes) / ls.records;
        std::printf("== upload: carga inicial ==\n");
        std::printf("arquivo de entrada (CSV)       : %s\n", csv_path.c_str());
        std::printf("arquivo de dados               : %s\n", data_path().c_str());
        std::printf("indice primario (ID)           : %s\n", primary_index_path().c_str());
        std::printf("indice secundario (Titulo)     : %s\n", secondary_index_path().c_str());
        std::printf("BLOCK_SIZE                     : %zu bytes\n", blockio::kBlockSize);

        std::printf("\n-- Parte 1: arquivo de dados organizado por hashing --\n");
        std::printf("registros carregados           : %llu\n", ull(ls.records));
        std::printf("linhas ignoradas (malformadas) : %llu\n", ull(ignoradas));
        std::printf("registros truncados            : %llu\n", ull(truncados));
        std::printf("layout do registro             : tamanho variavel (%zu bytes fixos + titulo + autores + snippet)\n",
                    kRecordFixedSize);
        std::printf("tamanho do registro            : medio %.1f bytes (min %zu, max %zu)\n", media_reg, menor_reg,
                    maior_reg);
        std::printf("bytes uteis por bloco          : %zu (cabecalho do bloco: %zu bytes)\n", kDataPayload,
                    kDataBlockHeaderSize);
        std::printf("funcao de hashing              : h(ID) = ID mod N\n");
        std::printf("numero de buckets (N)          : %u (primo; ocupacao alvo %.0f%%)\n", ls.num_buckets,
                    kHashLoadFactor * 100);
        std::printf("blocos do arquivo de dados     : %llu (1 cabecalho + %u primarios + %u overflow)\n",
                    ull(ls.total_blocks), ls.num_buckets, ls.overflow_blocks);
        std::printf("registros por bloco            : media %.2f (min %u, max %u); "
                    "capacidade para o registro medio = %zu\n",
                    static_cast<double>(ls.records) / ls.data_blocks, ls.min_recs_per_block, ls.max_recs_per_block,
                    static_cast<std::size_t>(kDataPayload / media_reg));
        std::printf("taxa de ocupacao dos blocos    : %.1f%% (bytes de registros / bytes dos %llu blocos de dados)\n",
                    pct(static_cast<double>(ls.record_bytes), static_cast<double>(ls.data_blocks) * blockio::kBlockSize),
                    ull(ls.data_blocks));
        std::printf("ocupacao dos blocos primarios  : %.1f%% (registros / capacidade se nao houvesse overflow)\n",
                    pct(static_cast<double>(ls.record_bytes), static_cast<double>(ls.num_buckets) * kDataPayload));
        std::printf("buckets vazios                 : %u\n", ls.empty_buckets);
        std::printf("buckets com overflow           : %u (%.1f%%)\n", ls.buckets_with_overflow,
                    pct(ls.buckets_with_overflow, ls.num_buckets));
        std::printf("maior cadeia de um bucket      : %u blocos\n", ls.max_chain_len);
        std::printf("maior bucket                   : %u registros (media %.2f por bucket)\n", ls.max_recs_per_bucket,
                    static_cast<double>(ls.records) / ls.num_buckets);
        std::printf("comprimento das cadeias        :");
        for (std::size_t k = 1; k < ls.chain_len_hist.size(); ++k)
            if (ls.chain_len_hist[k]) std::printf(" [%zu bloco(s): %u buckets]", k, ls.chain_len_hist[k]);
        std::printf("\n");
        std::printf("registros por bucket           :");
        for (std::size_t k = 0; k < ls.bucket_size_hist.size(); ++k)
            if (ls.bucket_size_hist[k]) std::printf(" %zu:%u", k, ls.bucket_size_hist[k]);
        std::printf("\n");
        std::printf("custo da carga (dados)         : %llu blocos lidos, %llu blocos escritos\n",
                    ull(custo_dados.reads), ull(custo_dados.writes));

        std::printf("\n-- Parte 2: indices B+Tree (construidos por insercao, na ordem fisica do arquivo de dados) --\n");
        std::printf("varredura do arquivo de dados  : %llu blocos lidos\n", ull(custo_varredura.reads));
        print_index_stats("indice primario (ID)", primario, entradas);
        print_index_stats("indice secundario (Titulo)", secundario, entradas);
        std::printf("\ntempo total: %.0f ms (informativo; nao e a metrica do trabalho)\n", ms);

        // Linha única, fácil de extrair por scripts (scripts/run_queries.sh).
        std::printf("RESUMO upload block_size=%zu registros=%llu buckets=%u overflow=%u blocos_dados=%llu "
                    "escritas_dados=%llu leituras_dados=%llu "
                    "idx1_altura=%u idx1_blocos=%llu idx1_leituras=%llu idx1_escritas=%llu "
                    "idx2_altura=%u idx2_blocos=%llu idx2_leituras=%llu idx2_escritas=%llu varredura=%llu\n",
                    blockio::kBlockSize, ull(ls.records), ls.num_buckets, ls.overflow_blocks, ull(ls.total_blocks),
                    ull(custo_dados.writes), ull(custo_dados.reads), primario.height(),
                    ull(primario.block_count()), ull(primario.stats().reads), ull(primario.stats().writes),
                    secundario.height(), ull(secundario.block_count()), ull(secundario.stats().reads),
                    ull(secundario.stats().writes), ull(custo_varredura.reads));
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "erro: %s\n", e.what());
        return 1;
    }
}
