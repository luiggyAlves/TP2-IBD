// datafile.hpp — arquivo de dados paginado organizado por hashing (Parte 1).
//
// Visão geral do arquivo artigos.dat (todos os blocos têm BLOCK_SIZE bytes):
//
//   bloco 0            cabeçalho do arquivo (número de buckets N, contadores)
//   blocos 1 .. N      bloco primário de cada bucket (bucket b -> bloco 1 + b)
//   blocos N+1 .. fim  blocos de overflow, alocados sob demanda e encadeados
//                      ao último bloco da cadeia do seu bucket
//
// Layout de um bloco de dados (primário ou overflow):
//
//   offset  tamanho  campo
//   0       4        next    : bloco seguinte da cadeia de overflow (0 = fim)
//   4       4        bucket  : número do bucket a que o bloco pertence
//   8       2        count   : número de registros no bloco
//   10      2        used    : bytes de registros ocupados após o cabeçalho
//   12      4        reservado
//   16      ...      registros de tamanho variável, um após o outro
//
// Layout de um registro (tamanho variável, com indicadores de comprimento):
//
//   offset  tamanho  campo
//   0       2        rec_len      : tamanho total do registro em bytes
//   2       4        id
//   6       4        ano
//   10      4        citacoes
//   14      19       atualizacao  : "AAAA-MM-DD HH:MM:SS" (tamanho fixo)
//   33      2        len_titulo
//   35      2        len_autores
//   37      2        len_snippet
//   39      ...      bytes do título, depois autores, depois snippet
//
// Como não existe remoção nem atualização, os registros ficam compactados no
// bloco e o endereço físico de um registro (RID) é simplesmente o par
// (número do bloco, deslocamento do registro dentro do bloco).
//
// Todos os inteiros são gravados na ordem de bytes nativa da máquina (x86-64,
// little-endian), via memcpy — o arquivo só é lido pelos próprios programas.

#ifndef DATAFILE_HPP
#define DATAFILE_HPP

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "blockio.hpp"
#include "record.hpp"

namespace tp2 {

// ---------------------------------------------------------------------------
// Endereço físico de registro (Record ID)
// ---------------------------------------------------------------------------
struct Rid {
    std::uint32_t block = 0;   // número do bloco no arquivo de dados
    std::uint16_t offset = 0;  // deslocamento do registro dentro do bloco
};

// ---------------------------------------------------------------------------
// Constantes de layout
// ---------------------------------------------------------------------------
constexpr std::size_t kDataBlockHeaderSize = 16;  // cabeçalho de cada bloco de dados
constexpr std::size_t kDataPayload = blockio::kBlockSize - kDataBlockHeaderSize;  // bytes úteis
constexpr std::size_t kRecordFixedSize = 39;      // parte fixa do registro
constexpr std::size_t kDateLen = 19;              // "AAAA-MM-DD HH:MM:SS"

// O cabeçalho usa uint16_t para 'used' e 'rec_len'; garante que caibam.
static_assert(blockio::kBlockSize <= 65535, "BLOCK_SIZE precisa caber em uint16_t");

// ---------------------------------------------------------------------------
// Serialização de registros
// ---------------------------------------------------------------------------

// Tamanho que o registro ocupa no bloco (parte fixa + bytes dos 3 textos).
std::size_t record_size(const Article& a);

// Garante que o registro caiba em um bloco, truncando (nesta ordem) o snippet,
// os autores e o título. Devolve true se algum campo foi truncado.
// Na amostra oficial nenhum registro precisa ser truncado (o maior tem
// ~1,3 KB), mas a proteção evita corromper o arquivo com entradas anômalas.
bool fit_record(Article& a, std::size_t max_size);

// Grava o registro em 'dst' (que deve ter record_size(a) bytes livres).
void encode_record(const Article& a, std::uint8_t* dst);

// Lê o registro que começa em 'src'. Devolve o tamanho do registro (rec_len).
std::size_t decode_record(const std::uint8_t* src, Article& out);

// Lê apenas o ID do registro em 'src' (evita copiar os textos ao procurar).
int peek_record_id(const std::uint8_t* src);

// ---------------------------------------------------------------------------
// Cabeçalho de bloco de dados
// ---------------------------------------------------------------------------
struct DataBlockHeader {
    std::uint32_t next = 0;    // próximo bloco da cadeia (0 = não há)
    std::uint32_t bucket = 0;  // bucket dono do bloco
    std::uint16_t count = 0;   // registros no bloco
    std::uint16_t used = 0;    // bytes de registros ocupados
};

DataBlockHeader get_block_header(const std::uint8_t* block);
void put_block_header(std::uint8_t* block, const DataBlockHeader& h);

// Percorre os registros de um bloco já carregado em memória, chamando
// fn(offset, ponteiro_para_o_registro) para cada um. Não faz I/O.
void for_each_record_in_block(const std::uint8_t* block,
                              const std::function<void(std::uint16_t, const std::uint8_t*)>& fn);

// ---------------------------------------------------------------------------
// Cabeçalho do arquivo (bloco 0)
// ---------------------------------------------------------------------------
struct HashFileHeader {
    std::uint32_t block_size = 0;       // BLOCK_SIZE com que o arquivo foi criado
    std::uint32_t num_buckets = 0;      // N
    std::uint64_t num_records = 0;      // registros gravados
    std::uint32_t overflow_blocks = 0;  // blocos de overflow alocados
    std::uint64_t record_bytes = 0;     // soma dos tamanhos dos registros
};

// Função de hashing: h(ID) = ID mod N (N primo). Ver relatório, Seção 7.1.
inline std::uint32_t hash_bucket(int id, std::uint32_t num_buckets) {
    return static_cast<std::uint32_t>(static_cast<std::uint32_t>(id) % num_buckets);
}

// Bloco primário do bucket b (o bloco 0 é o cabeçalho).
inline std::uint32_t bucket_block(std::uint32_t bucket) { return 1 + bucket; }

// Menor primo >= n (usado para escolher N).
std::uint32_t next_prime(std::uint32_t n);

// ---------------------------------------------------------------------------
// Estatísticas da carga (calculadas em memória durante o upload)
// ---------------------------------------------------------------------------
struct LoadStats {
    std::uint64_t records = 0;
    std::uint64_t record_bytes = 0;
    std::uint32_t num_buckets = 0;
    std::uint32_t overflow_blocks = 0;
    std::uint64_t data_blocks = 0;       // primários + overflow (sem o cabeçalho)
    std::uint64_t total_blocks = 0;      // inclui o cabeçalho
    std::uint32_t min_recs_per_block = 0;
    std::uint32_t max_recs_per_block = 0;
    std::uint32_t empty_buckets = 0;
    std::uint32_t buckets_with_overflow = 0;
    std::uint32_t max_chain_len = 0;     // em blocos (1 = sem overflow)
    std::uint32_t max_recs_per_bucket = 0;
    std::vector<std::uint32_t> chain_len_hist;     // [k] = buckets com cadeia de k blocos
    std::vector<std::uint32_t> bucket_size_hist;   // [k] = buckets com k registros
};

// ---------------------------------------------------------------------------
// Arquivo de dados por hashing
// ---------------------------------------------------------------------------
class HashFile {
  public:
    // ---- construção (usado apenas pelo upload) ----

    // Cria o arquivo do zero com N buckets: grava o cabeçalho e os N blocos
    // primários vazios.
    void create(const std::string& path, std::uint32_t num_buckets);

    // Insere um registro no bucket h(ID). Lê o último bloco da cadeia do
    // bucket (1 leitura); se o registro couber, regrava o bloco (1 escrita);
    // senão, aloca um bloco de overflow, encadeia-o e grava os dois blocos.
    // O registro precisa caber em kDataPayload (ver fit_record).
    Rid insert(const Article& a);

    // Grava o cabeçalho definitivo e devolve as estatísticas da carga.
    LoadStats finish();

    // ---- consulta ----

    // Abre um arquivo existente e lê o cabeçalho (1 leitura de bloco).
    void open(const std::string& path);

    const HashFileHeader& header() const { return header_; }

    // Busca por ID pela organização hash: bloco primário do bucket h(ID) e,
    // se necessário, os blocos de overflow encadeados. 'chain_read' recebe o
    // número de blocos do bucket que foram lidos.
    bool find(int id, Article& out, Rid& rid, std::uint32_t& chain_read);

    // Varredura sequencial: lê todos os blocos de dados (1 .. fim), em ordem
    // física, chamando fn(rid, registro_serializado) para cada registro.
    void scan(const std::function<void(const Rid&, const std::uint8_t*)>& fn);

    blockio::BlockFile& file() { return file_; }

  private:
    blockio::BlockFile file_;
    HashFileHeader header_;
    std::vector<std::uint8_t> buf_ = std::vector<std::uint8_t>(blockio::kBlockSize);

    // Estado mantido em memória SOMENTE durante a carga (catálogo da carga).
    // Evita percorrer a cadeia inteira a cada inserção: insere-se sempre no
    // último bloco da cadeia do bucket.
    std::vector<std::uint32_t> tail_block_;    // último bloco da cadeia de cada bucket
    std::vector<std::uint32_t> chain_len_;     // blocos na cadeia de cada bucket
    std::vector<std::uint32_t> bucket_recs_;   // registros em cada bucket
    std::vector<std::uint16_t> block_recs_;    // registros em cada bloco (índice = bloco)
    std::uint32_t next_free_block_ = 0;        // próximo bloco a ser alocado no fim do arquivo

    void write_header();
};

// Lê o registro apontado por 'rid' (1 leitura de bloco). Usado pelos
// programas da Parte 2, que chegam ao registro pelo RID guardado no índice.
// Devolve false se o deslocamento não apontar para um registro válido.
bool read_record_at(blockio::BlockFile& f, const Rid& rid, Article& out);

// ---------------------------------------------------------------------------
// Impressão de registros
// ---------------------------------------------------------------------------

// Todos os campos, um por linha (findrec, findtitle, seek1, seek2).
void print_record(const Article& a, const Rid& rid);

// Todos os campos em uma linha (findrange e seekrange, que podem devolver
// milhares de registros).
void print_record_line(const Article& a);

}  // namespace tp2

#endif  // DATAFILE_HPP
