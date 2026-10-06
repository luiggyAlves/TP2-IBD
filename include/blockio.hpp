// blockio — camada de acesso a blocos do TP2 (IBD 2026/02).
//
// Esta é a única forma admitida de ler e escrever nos arquivos de dados e de
// índice. Todo acesso passa por aqui para que a contagem de blocos lidos e
// escritos seja determinística e comparável entre implementações diferentes.
//
// O tamanho do bloco é definido em tempo de compilação. Para o experimento da
// Seção 7.5 do enunciado, recompile com -DBLOCK_SIZE=8192.

#ifndef BLOCKIO_HPP
#define BLOCKIO_HPP

#include <cstddef>
#include <cstdint>
#include <string>

#ifndef BLOCK_SIZE
#define BLOCK_SIZE 4096
#endif

namespace blockio {

constexpr std::size_t kBlockSize = BLOCK_SIZE;

struct Stats {
    std::uint64_t reads = 0;   // blocos lidos desde a abertura
    std::uint64_t writes = 0;  // blocos escritos desde a abertura
};

enum class Mode {
    kRead,       // abre para leitura; falha se o arquivo não existir
    kReadWrite,  // abre para leitura e escrita; cria se não existir
    kTruncate    // cria do zero, descartando conteúdo anterior
};

// Arquivo paginado em blocos de kBlockSize bytes.
// Os blocos são numerados a partir de 0.
class BlockFile {
  public:
    BlockFile() = default;
    ~BlockFile();

    BlockFile(const BlockFile&) = delete;
    BlockFile& operator=(const BlockFile&) = delete;

    // Abre o arquivo. Lança std::runtime_error em caso de falha.
    void open(const std::string& path, Mode mode);
    void close();
    bool is_open() const { return fd_ >= 0; }

    // Lê o bloco 'index' para 'buffer', que deve ter ao menos kBlockSize bytes.
    // Incrementa o contador de leituras.
    void read_block(std::uint64_t index, void* buffer);

    // Escreve kBlockSize bytes de 'buffer' no bloco 'index'.
    // Incrementa o contador de escritas.
    void write_block(std::uint64_t index, const void* buffer);

    // Acrescenta um bloco zerado ao final e devolve seu índice.
    std::uint64_t alloc_block();

    // Número de blocos do arquivo.
    std::uint64_t block_count() const;

    const Stats& stats() const { return stats_; }
    void reset_stats() { stats_ = Stats{}; }

    const std::string& path() const { return path_; }

  private:
    int fd_ = -1;
    std::string path_;
    Stats stats_;
};

}  // namespace blockio

#endif  // BLOCKIO_HPP
